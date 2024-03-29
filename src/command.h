// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

namespace OctoPrintControl::Commands {

class BotCommand {
public:
    BotCommand();
    virtual ~BotCommand() {}

    virtual std::string Id() = 0;
    virtual std::string Description() = 0;
    virtual void Run(std::string channel, std::string message, std::string author, std::vector<std::string> args) = 0;

protected:
    std::shared_ptr<spdlog::logger> log;
};

class Help : public BotCommand {
public:
    Help():BotCommand() {}

    std::string Id() { return "help"; }
    std::string Description() { return "Lists available OctoPrintControl commands."; }

    void Run(std::string channel, std::string message, std::string author, std::vector<std::string> args);
};

class Ping : public BotCommand {
public:
    Ping():BotCommand() {}

    std::string Id() { return "ping"; }
    std::string Description() { return "A command to test that the bot application is running. It will also return some version and connection info."; }

    void Run(std::string channel, std::string message, std::string author, std::vector<std::string> args);
};

class ListPrinters : public BotCommand {
public:
    ListPrinters():BotCommand() {}

    std::string Id() { return "list-printers"; }
    std::string Description() { return "Returns a list of printers this bot can interact with and monitor."; }

    void Run(std::string channel, std::string message, std::string author, std::vector<std::string> args);
};

class PowerOn : public BotCommand {
public:
    PowerOn():BotCommand() {}

    std::string Id() { return "power-on"; }
    std::string Description() { return "Power on a printer using the PSU Control plugin."; }

    void Run(std::string channel, std::string message, std::string author, std::vector<std::string> args);
};

class PowerOff : public BotCommand {
public:
    PowerOff():BotCommand() {}

    std::string Id() { return "power-off"; }
    std::string Description() { return "Power off a printer using the PSU Control plugin."; }

    void Run(std::string channel, std::string message, std::string author, std::vector<std::string> args);
};

class PrinterStatus : public BotCommand {
public:
    PrinterStatus():BotCommand() {}

    std::string Id() { return "printer-status"; }
    std::string Description() { return "Display current printer status and webcam view."; }

    void Run(std::string channel, std::string message, std::string author, std::vector<std::string> args);
};

}