// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include "websocket.h"
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <stdexcept>
#include <chrono>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace OctoPrintControl::Websocket {

Client::Client(std::string url)
:url(url) {
    this->curl = curl_easy_init();

    this->log = spdlog::get(fmt::format("Websocket::Client::{}", url));
    if (!this->log.get()) this->log = spdlog::stdout_color_mt(fmt::format("Websocket::Client::{}", url));
}

Client::~Client() {
    this->Disconnect();
    if (this->thread.joinable()) this->thread.join();
    curl_easy_cleanup(this->curl);
}

void Client::Connect() {
    curl_easy_reset(this->curl);
    curl_easy_setopt(this->curl, CURLOPT_URL, this->url.c_str());
    curl_easy_setopt(this->curl, CURLOPT_USERAGENT, this->userAgent);
    curl_easy_setopt(this->curl, CURLOPT_CONNECT_ONLY, 2L);

    this->log->debug("Connecting...");

    CURLcode res = curl_easy_perform(this->curl);
    if (res!=CURLE_OK) {
        int respcode;
        curl_easy_getinfo(this->curl, CURLINFO_RESPONSE_CODE, &respcode);
        std::string msg = fmt::format("Couldn't connect websocket at {}: {}", this->url, respcode);
        throw std::runtime_error(msg);
    }

    curl_easy_getinfo(this->curl, CURLINFO_ACTIVESOCKET, &this->socket);

    this->log->debug("Connected.");

    this->connected = true;
    this->thread = std::thread(&Client::ThreadMain, this);
}

void Client::Disconnect() {
    this->connected = false;
}

void Client::UserAgent(std::string userAgent) {
    this->userAgent = userAgent;
}

void Client::Send(std::vector<char> data) {
    this->sendQueue.push(data);
}

void Client::Send(std::string data) {
    this->Send(std::vector<char>(data.begin(), data.end()));
}

void Client::AddDataReceivedCallback(DataReceivedCallback cb) {
    this->callbacks.push_back(cb);
}

void Client::ThreadMain() {
    char *buf = new char[1024];
    std::vector<char> data;
    size_t recv = 0;
    const struct curl_ws_frame *frame;
    
    CURLcode res;
    while (this->connected) {
        //data.clear();
        bool lastCont = false;
        do {
            res = curl_ws_recv(this->curl, buf, 1024, &recv, &frame);
            if (res==CURLE_GOT_NOTHING) {
                spdlog::error("Websocket disconnected.");
                this->connected = false;
                break;
            }
            for (size_t i=0;i<recv;i++) data.push_back(buf[i]);
            if (res==CURLE_OK) lastCont = frame->flags & CURLWS_CONT;
        } while (res==CURLE_OK && recv > 0);

        if (data.size() && !lastCont) {
            //this->log->debug("Received: {}", std::string(data.begin(), data.end()));
            for (DataReceivedCallback cb : this->callbacks) cb(data);
            data.clear();
        }

        while (this->sendQueue.size()) {
            std::vector<char> d = this->sendQueue.front();
            size_t sent = 0;
            //this->log->debug("Sending: {}", std::string(d.begin(), d.end()));
            if (curl_ws_send(this->curl, d.data(), d.size(), &sent, 0, CURLWS_TEXT)) {
                this->log->warn("Couldn't send data on websocket.");
                this->connected = false;
                break;
            }
            this->sendQueue.pop();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }


    delete[] buf;
}

}