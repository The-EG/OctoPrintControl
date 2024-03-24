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

typedef std::function<void(std::string channel, std::string message, std::string author, std::vector<std::string> args)> CommandHandler;

class InteractionHandler {
public:
    std::chrono::steady_clock::time_point expires;
    
    virtual bool HandleInteraction(std::string id, std::string token, std::string response) = 0;
    virtual void ExpireInteraction() = 0;

protected:
    InteractionHandler(std::string token):bot_token(token) {}
    std::string bot_token;
};

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

    void CommandPing(std::string channel, std::string message, std::string author, std::vector<std::string> args);
    void CommandListPrinters(std::string channel, std::string message, std::string author, std::vector<std::string> args);
    void CommandPowerOn(std::string channel, std::string message, std::string author, std::vector<std::string> args);
    void CommandPowerOff(std::string channel, std::string message, std::string author, std::vector<std::string> args);
    void CommandPrinterStatus(std::string channel, std::string message, std::string author, std::vector<std::string> args);

    std::shared_ptr<Discord::Channel> GetChannel(std::string channel_id);

    bool ValidateCommandPrinterArg(std::vector<std::string> args, std::string channel, std::string message);

    nlohmann::json config;

    std::string user_id;
    std::string update_channel;

    std::set<std::string> trusted_users;

    uint64_t print_update_freq;

    bool running = false;

    std::shared_ptr<spdlog::logger> log;

    std::shared_ptr<Discord::Socket> gateway;

    std::map<std::string, CommandHandler> commands;
    std::map<std::string, std::shared_ptr<InteractionHandler>> interactions;

    std::map<std::string, std::shared_ptr<Printer>> printers;
    std::map<std::string, std::chrono::steady_clock::time_point> print_update_times;

    std::map<std::string, std::shared_ptr<Discord::Channel>> channel_cache;
};

class PrinterPowerOffInteraction : public InteractionHandler {
public:
    PrinterPowerOffInteraction(std::string token, std::shared_ptr<Printer> printer, std::string channel, std::string message, std::string reference);

    bool HandleInteraction(std::string id, std::string token, std::string response);
    void ExpireInteraction();
private:
    std::shared_ptr<Printer> printer;
    std::string reference_id;
    std::string message_id;
    std::string channel_id;
};

}