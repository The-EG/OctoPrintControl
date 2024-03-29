// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <chrono>
#include <string>
#include <memory>

#include "printer.h"

namespace OctoPrintControl::Interactions {

class InteractionHandler {
public:
    std::chrono::steady_clock::time_point expires;
    
    virtual bool HandleInteraction(std::string id, std::string token, std::string response) = 0;
    virtual void ExpireInteraction() = 0;

protected:
    InteractionHandler() {}
};


class PrinterPowerOffInteraction : public InteractionHandler {
public:
    PrinterPowerOffInteraction(std::shared_ptr<OctoPrintControl::Printer> printer, std::string channel, std::string message, std::string reference);

    bool HandleInteraction(std::string id, std::string token, std::string response);
    void ExpireInteraction();
private:
    std::shared_ptr<OctoPrintControl::Printer> printer;
    std::string reference_id;
    std::string message_id;
    std::string channel_id;
};

}