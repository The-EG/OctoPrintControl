// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <string>
#include <memory>
#include <ctime>
#include <spdlog/spdlog.h>

#include "octoprint.h"

namespace OctoPrintControl {

class Printer {
public:
    Printer(std::string name, std::string url, std::string apikey);

    std::string Name() { return name; }

    void PowerOff();
    void PowerOn();
    bool IsOn();
    
    std::shared_ptr<OctoPrint::Client> client;
    std::shared_ptr<OctoPrint::Socket> socket;

    bool IsConnected();
    bool IsPrinting();

    double Progress();

    std::string StatusText();
    time_t LastStatusTime();
    std::string FileDisplay();

    struct temp_data {
        double actual = -1;
        double target = -1;
    };

    std::map<std::string, std::shared_ptr<temp_data>> last_temps;

private:
    void OnSocketConnected(std::string msgtype, nlohmann::json data);
    void OnSocketCurrent(std::string msgtype, nlohmann::json data);

    struct {
        std::string desc = "Unknown";
        bool operational = false;
        bool paused = false;
        bool printing = false;
        bool pausing = false;
        bool cancelling = false;
        bool sdready = false;
        bool error = false;
        bool ready = false;
        bool closedorerror = true;
    } last_state;

    
    time_t last_current = 0;

    std::string file_display;

    uint64_t print_time;
    uint64_t print_time_left;

    std::string name;
    std::string url;
    std::string apikey;
    std::shared_ptr<spdlog::logger> log;
};

}