// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include "http.h"
#include <fmt/core.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <stdexcept>

namespace OctoPrintControl::HTTP {

void MultiPartRequestData::AddPart(std::string name, std::string data) {
    std::shared_ptr<Part> p(new Part);
    
    p->name = name;
    p->data = std::vector<char>(data.begin(), data.end());

    this->parts.push_back(p);
}

void MultiPartRequestData::AddFile(std::string name, std::string filename, std::string filetype, std::vector<char> data) {
    std::shared_ptr<Part> p(new Part);

    p->name = name;
    p->filename = filename;
    p->filetype = filetype;
    p->data = std::vector<char>(data.begin(), data.end());
    
    this->parts.push_back(p);
}

curl_mime *MultiPartRequestData::ToMime(CURL *curl) {
    curl_mime *mime = curl_mime_init(curl);

    for (std::shared_ptr<Part> p : this->parts) {
        curl_mimepart *part = curl_mime_addpart(mime);
        curl_mime_name(part, p->name.c_str());
        curl_mime_data(part, p->data.data(), p->data.size());
        if (p->filename.size()) curl_mime_filename(part, p->filename.c_str());
        if (p->filetype.size()) curl_mime_type(part, p->filetype.c_str());
    }

    return mime;
}


void Client::AddHeader(std::string header) {
    this->headers.push_back(header);
}

static size_t DataWriteCallback(char *ptr, size_t size, size_t nmemb, void *user) {
    std::vector<char> *data = static_cast<std::vector<char>*>(user);

    for(size_t i=0;i<nmemb;i++) data->push_back(ptr[i]);

    return nmemb;
}

Client::Client() {
    this->log = spdlog::get("HTTP::Client");
    if (!this->log.get()) this->log = spdlog::stdout_color_mt("HTTP::Client");
    this->curl = curl_easy_init(); 
}

std::shared_ptr<Response> Client::Perform(std::shared_ptr<Request> request) {
    std::lock_guard<std::mutex> curl_lock(this->curl_mutex);

    std::shared_ptr<Response> resp(new Response);

    curl_easy_reset(this->curl);

    if (this->userAgent.size()) curl_easy_setopt(this->curl, CURLOPT_USERAGENT, this->userAgent.c_str());

    // build headers
    curl_slist *hdrs = nullptr;
    // first from the headers in this client
    for (std::string sh : this->headers) hdrs = curl_slist_append(hdrs, sh.c_str());
    // then from those in the request
    for (std::string rh : request->headers) hdrs = curl_slist_append(hdrs, rh.c_str());

    // add a header if we are sending JSON
    if (request->body.get() && request->body->DataType()==RequestDataType::JSON) {
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    }

    curl_easy_setopt(this->curl, CURLOPT_HTTPHEADER, hdrs);

    curl_easy_setopt(this->curl, CURLOPT_URL, request->url.c_str());

    std::string method = "";
    switch(request->method) {
    case RequestMethod::GET:
        method = "GET";
        break;
    case RequestMethod::POST:
        method = "POST";
        curl_easy_setopt(this->curl, CURLOPT_POST, 1L);
        if (!request->body.get()) curl_easy_setopt(this->curl, CURLOPT_POSTFIELDSIZE, 0);
        break;
    case RequestMethod::PUT:
        method = "PUT";
        curl_easy_setopt(this->curl, CURLOPT_UPLOAD, 1L);
        if (!request->body.get()) curl_easy_setopt(this->curl, CURLOPT_INFILESIZE, 0);
        break;
    case RequestMethod::PATCH:
        method = "PATCH";
        curl_easy_setopt(this->curl, CURLOPT_CUSTOMREQUEST, "PATCH");
        break;
    case RequestMethod::DELETE:
        method = "DELETE";
        curl_easy_setopt(this->curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        break;
    }

    curl_mime *mime = nullptr;

    if (request->body.get()) {
        if (request->body->DataType()==RequestDataType::JSON) {
            std::shared_ptr<JSONRequestData> json = std::dynamic_pointer_cast<JSONRequestData>(request->body);
            curl_easy_setopt(this->curl, CURLOPT_COPYPOSTFIELDS, json->data.dump().c_str());
        } else if (request->body->DataType()==RequestDataType::MultiPart) {
            std::shared_ptr<MultiPartRequestData> mpd = std::dynamic_pointer_cast<MultiPartRequestData>(request->body);
            mime = mpd->ToMime(this->curl);
            curl_easy_setopt(this->curl, CURLOPT_MIMEPOST, mime);
        }
    }

    curl_easy_setopt(this->curl, CURLOPT_WRITEFUNCTION, &DataWriteCallback);
    curl_easy_setopt(this->curl, CURLOPT_WRITEDATA, &resp->body);

    CURLcode r = curl_easy_perform(this->curl);
    if (r!=CURLE_OK) {
        this->log->error("curl error: {}", curl_easy_strerror(r));
        throw std::runtime_error(fmt::format("curl error: {}", curl_easy_strerror(r)));
    }

    curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &resp->code);

    if (resp->code >= 200 && resp->code < 300) {
        this->log->info("{} {} -> {}", method, request->url, resp->code);
    } else {
        this->log->warn("{} {} -> {}", method, request->url, resp->code);
    }
    
    struct curl_header *ct;
    curl_easy_header(this->curl, "Content-Type", 0, CURLH_HEADER, -1, &ct);
    resp->contentType = ct->value;

    if (mime) curl_mime_free(mime);
    curl_slist_free_all(hdrs);

    return resp;
}

std::string Client::EscapeString(std::string str) {
    char *enc = curl_easy_escape(this->curl, str.c_str(), (int)str.length());
    std::string encstr = enc;
    curl_free(enc);
    return encstr;
}

std::shared_ptr<Request> NewPutRequest(std::string url) {
    std::shared_ptr<Request> req(new Request);
    req->method = RequestMethod::PUT;
    req->url = url;

    return req;
}

std::shared_ptr<Request> NewGetRequest(std::string url) {
    std::shared_ptr<Request> req(new Request);
    req->method = RequestMethod::GET;
    req->url = url;

    return req;
}

}