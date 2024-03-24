// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include "printer.h"
#include <spdlog/sinks/stdout_color_sinks.h>

namespace OctoPrintControl {

Printer::Printer(std::string name, std::string url, std::string apikey) 
:name(name), url(url), apikey(apikey) {
    this->client.reset(new OctoPrint::Client(name, url, apikey));
    this->socket.reset(new OctoPrint::Socket("ws" + url.substr(4)));

    this->socket->AddCallback("connected", std::bind(&Printer::OnSocketConnected, this, std::placeholders::_1, std::placeholders::_2));
    this->socket->AddCallback("current", std::bind(&Printer::OnSocketCurrent, this, std::placeholders::_1, std::placeholders::_2));

    this->log = spdlog::get("Printer::" + name);
    if (!this->log.get()) this->log = spdlog::stdout_color_mt("Printer::" + name);

    std::thread t(&OctoPrint::Socket::Connect, this->socket);
    t.detach();
}

void Printer::OnSocketConnected(std::string msgtype, nlohmann::json data) {
    this->log->info("Socket connected, subscribing and authenticating");

    nlohmann::json sub = {{
        "subscribe", {
            { "state", {
                { "logs", false },
                { "messages", false }
            }},
            {"plugins", true},
            {"events", true}
        }
    }};

    this->socket->Send(sub);

    try {
        nlohmann::json session = this->client->PassiveLogin();
        nlohmann::json auth = {
            { "auth", session["name"].get<std::string>() + ":" + session["session"].get<std::string>() }
        };
        this->socket->Send(auth);
    } catch (std::runtime_error &err) {
        this->log->error("Couldn't authenticate: {}", err.what());
    }
}

void Printer::PowerOff() {
    this->client->PluginSimpleApiCommand("psucontrol", nlohmann::json({{"command", "turnPSUOff"}}));
}

void Printer::PowerOn() {
    this->client->PluginSimpleApiCommand("psucontrol", nlohmann::json({{"command", "turnPSUOn"}}));
}


bool Printer::IsOn() {
    nlohmann::json msg = this->client->PluginSimpleApiCommand("psucontrol", nlohmann::json({{"command", "getPSUState"}}));

    return msg.at("isPSUOn").get<bool>();
}

bool Printer::IsConnected() {
    return !this->last_state.closedorerror;
}

bool Printer::IsPrinting() {
    return this->last_state.printing;
}

double Printer::Progress() {
    if (this->print_time + this->print_time_left == 0) return 0.0;
    return (double)this->print_time / (this->print_time_left + this->print_time);
}

std::string Printer::StatusText() {
    return this->last_state.desc;
}

time_t Printer::LastStatusTime() {
    return this->last_current;
}

std::string Printer::FileDisplay() {
    return this->file_display;
}

void Printer::OnSocketCurrent(std::string msgtype, nlohmann::json data) {
    //this->log->debug("Current: {}", data.dump());
    nlohmann::json &state = data.at("state");
    nlohmann::json &flags = state.at("flags");
    state.at("text").get_to(this->last_state.desc);
    flags.at("operational").get_to(this->last_state.operational);
    flags.at("paused").get_to(this->last_state.paused);
    flags.at("printing").get_to(this->last_state.printing);
    flags.at("pausing").get_to(this->last_state.pausing);
    flags.at("cancelling").get_to(this->last_state.cancelling);
    flags.at("sdReady").get_to(this->last_state.sdready);
    flags.at("error").get_to(this->last_state.error);
    flags.at("ready").get_to(this->last_state.ready);
    flags.at("closedOrError").get_to(this->last_state.closedorerror);

    nlohmann::json &progress = data.at("progress");
    if (progress.at("printTime").is_number()) progress.at("printTime").get_to(this->print_time);
    else this->print_time = 0;
    if (progress.at("printTimeLeft").is_number()) progress.at("printTimeLeft").get_to(this->print_time_left);
    else this->print_time_left = 0;

    this->last_current = time(NULL);

    nlohmann::json &file = data.at("job").at("file");
    if (file.contains("display") && file.at("display").is_string()) file.at("display").get_to(this->file_display);
    else this->file_display = "";

    if (data.at("temps").size()) {
        nlohmann::json &temps = data.at("temps")[0];
        for (auto &[key, t] : temps.items()) {
            // skip time and anything that doesn't have an actual
            if (!t.is_object() || t.at("actual").is_null()) continue;
            
            if (!this->last_temps.contains(key)) this->last_temps[key] = std::shared_ptr<temp_data>(new temp_data);
            if (t.at("actual").is_number()) t.at("actual").get_to(this->last_temps[key]->actual);
            if (t.at("target").is_number()) t.at("target").get_to(this->last_temps[key]->target);
        }
    }
}

}