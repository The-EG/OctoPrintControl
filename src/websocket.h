// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <string>
#include <functional>
#include <vector>
#include <list>
#include <queue>
#include <thread>
#include <spdlog/spdlog.h>
#include <curl/curl.h>

namespace OctoPrintControl::Websocket {

typedef std::function<void(std::vector<char>)> DataReceivedCallback;

class Client {
public:
    Client(std::string url);
    ~Client();
    void Connect();
    void Disconnect();

    void UserAgent(std::string userAgent);

    void Send(std::vector<char> data);
    void Send(std::string data);

    void AddDataReceivedCallback(DataReceivedCallback cb);

private:
    void ThreadMain();

    std::shared_ptr<spdlog::logger> log;

    CURL *curl;
    curl_socket_t socket;
    std::string url;
    bool connected;
    std::string userAgent;
    std::queue<std::vector<char>> sendQueue;
    std::list<DataReceivedCallback> callbacks;
    std::thread thread;
};

}