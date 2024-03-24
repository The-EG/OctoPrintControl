// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <thread>
#include <string>
#include <memory>
#include <functional>
#include <cinttypes>
#include <vector>
#include <map>
#include <list>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "http.h"
#include "websocket.h"

namespace OctoPrintControl::OctoPrint {

class Client {
public:
    Client(std::string name, std::string url, std::string apikey);
    ~Client();

    std::vector<char> GetWebcamSnapshot(std::string &imageType);

    nlohmann::json PassiveLogin();

    nlohmann::json PluginSimpleApiCommand(std::string plugin, nlohmann::json data);

private:
    std::string name;
    std::string url;
    std::string apikey;

    std::shared_ptr<HTTP::Client> http;

    std::shared_ptr<spdlog::logger> log;
};

typedef std::function<void(std::string, nlohmann::json)> SocketDataCallback;

class Socket {
public:
    Socket(std::string url);
    ~Socket();

    void Connect();

    void AddCallback(std::string event, SocketDataCallback callback);

    void Send(nlohmann::json data);

private:
    void ProcessMessageArray(std::vector<char> data);
    void OnWebsocketData(std::vector<char> data);

    void WatchdogMain();

    std::chrono::steady_clock::time_point last_hb;

    std::shared_ptr<spdlog::logger> log;
    std::string baseurl;
    std::shared_ptr<Websocket::Client> websocket;
    std::map<std::string, std::list<SocketDataCallback>> callbacks;
};

}