// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <string>
#include <vector>
#include <list>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>

namespace OctoPrintControl::HTTP {

enum class RequestDataType {
    None,
    JSON,
    MultiPart
};

struct RequestDataBase {
    virtual ~RequestDataBase() {}
    virtual RequestDataType DataType() { return RequestDataType::None; }
};

struct JSONRequestData : public RequestDataBase {
    JSONRequestData(nlohmann::json &data) :data(data) {}
    RequestDataType DataType() { return RequestDataType::JSON; }

    nlohmann::json &data;
};

struct MultiPartRequestData : public RequestDataBase {
    struct Part {
        std::string name;
        std::vector<char> data;
        std::string filename;
        std::string filetype;
    };



    void AddPart(std::string name, std::string data);
    void AddFile(std::string name, std::string filename, std::string filetype, std::vector<char> data);
    RequestDataType DataType() { return RequestDataType::MultiPart; }

    curl_mime *ToMime(CURL *curl);

private:
    std::list<std::shared_ptr<Part>> parts;
};

#ifdef DELETE
#undef DELETE
#endif

enum class RequestMethod {
    GET,
    POST,
    PUT,
    PATCH,
    DELETE
};

struct Request {
    std::string url;
    RequestMethod method;
    std::list<std::string> headers;
    std::shared_ptr<RequestDataBase> body;
};

std::shared_ptr<Request> NewPutRequest(std::string url);
std::shared_ptr<Request> NewGetRequest(std::string url);

struct Response {
    int code;
    std::string contentType;
    std::vector<char> body;
};

class Client {
public:
    Client();
    Client(std::string userAgent) :Client() { this->userAgent = userAgent; }
    ~Client() { curl_easy_cleanup(this->curl); }

    void AddHeader(std::string header);

    std::shared_ptr<Response> Perform(std::shared_ptr<Request> request);

    std::string EscapeString(std::string str);

private:
    CURL *curl;
    std::list<std::string> headers;
    std::string userAgent;
    std::shared_ptr<spdlog::logger> log;

    std::mutex curl_mutex;
};

}