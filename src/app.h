// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <chrono>
#include <set>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "printer.h"
#include "discord.h"

namespace OctoPrintControl {

class App {
public:
    App(int argc, char *argv[]);
    ~App();
    
    int Run();
    void HandleSignal(int signum);

private:
    std::string token;

    void OnReady(std::string, nlohmann::json data);
    void OnNewMessage(std::string, nlohmann::json data);
    void OnNewInteraction(std::string, nlohmann::json data);
    void OnPrinterEvent(std::string printer_id, std::shared_ptr<Printer> printer, std::string, nlohmann::json data);

    std::string user_id;
    std::string update_channel;

    std::set<std::string> trusted_users;

    uint64_t print_update_freq;

    bool running = false;

    std::shared_ptr<spdlog::logger> log;

    std::map<std::string, std::chrono::steady_clock::time_point> print_update_times;
};



}