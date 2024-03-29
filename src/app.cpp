// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include "app.h"
#include <curl/curl.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>
#include <fmt/core.h>
#include <ranges>
#include <signal.h>
#include "utils.h"

#include "discord.h"
#include "octoprint.h"
#include "printer.h"
#include "version.h"
#include "octoprintcontrol.h"

#include <Magick++.h>

namespace OctoPrintControl {

#define BIND_COMMAND(cmd) std::bind(&cmd, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)

void App::HandleSignal(int signum) {
    switch(signum) {
    case SIGTERM:
        this->log->warn("Caught SIGTERM, shutting down...");
        break;
    case SIGINT:
        this->log->warn("Caught SIGINT, shutting down...");
        break;
    default:
        return;
    }
    this->running = false;
}

App::App(int argc, char *argv[]) {
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e - %n - %^%l%$ - %v");
    spdlog::set_level(spdlog::level::trace);
    spdlog::set_default_logger(spdlog::stdout_color_mt("OctoPrintControl"));

    this->log = spdlog::stdout_color_mt("App");
    
    this->log->info("===========================================================");
    this->log->info(" OctoPrint Control Init");
    this->log->info("-----------------------------------------------------------");
    this->log->info(" Copyright (c) 2024 Taylor Talkington");
    this->log->info(" License: MIT");
    this->log->info(" Version: {}.{}.{}", OCTOPRINTCONTROL_VERSION_MAJOR, OCTOPRINTCONTROL_VERSION_MINOR, OCTOPRINTCONTROL_VERSION_PATCH);
    this->log->info(" Git Commit: " OCTOPRINTCONTROL_GIT_HASH);
    this->log->info("-----------------------------------------------------------");

    Magick::InitializeMagick(argv[0]);

    std::filesystem::path conf_path = std::filesystem::current_path() / "OctoPrintControl.json";
    if (argc==2) {
        conf_path = argv[1];
    }

    this->log->info("Loading configuration from {}", conf_path.string());

    try {
        std::ifstream confstream(conf_path);
        ::OctoPrintControl::config = nlohmann::json::parse(confstream);
    } catch (nlohmann::json::parse_error &err) {
        this->log->critical("Couldn't parse config: {}", err.what());
        exit(-1);
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    try {
        ::OctoPrintControl::config.at("token").get_to(this->token);
    } catch (...) {
        this->log->error("Config must have a string value for `token`.");
        exit(-1);
    }

    ::OctoPrintControl::AddCommand(new Commands::Help);
    ::OctoPrintControl::AddCommand(new Commands::Ping);
    ::OctoPrintControl::AddCommand(new Commands::ListPrinters);
    ::OctoPrintControl::AddCommand(new Commands::PowerOn);
    ::OctoPrintControl::AddCommand(new Commands::PowerOff);
    ::OctoPrintControl::AddCommand(new Commands::PrinterStatus);

    try {
        ::OctoPrintControl::config.at("updateChannel").get_to(this->update_channel);
        this->log->info("Update Channel: {}", this->update_channel);
    } catch (...) {
        this->log->critical("No updateChannel specified in config.");
        exit(-1);
    }

    try {
        for (nlohmann::json &id : ::OctoPrintControl::config.at("trustedUsers")) {
            std::string uid = id.get<std::string>();
            this->trusted_users.insert(uid);
        }
        this->log->info("Trusted users:");
        for (std::string uid : this->trusted_users) {
            this->log->info("  {}", uid);
        }
    } catch (...) {
        this->log->warn("No trusted users specified in config.");
    }

    try {
        ::OctoPrintControl::config.at("printUpdateFreq").get_to(this->print_update_freq);
    } catch(...) {
        this->log->warn("No printUpdateFreq in config, using default.");
        this->print_update_freq = 600;
    }
    this->log->info("Print Update Message Frequency: {} seconds", this->print_update_freq);
}

App::~App() {
    ::OctoPrintControl::gateway.reset();
    for (auto &[id, printer] : ::OctoPrintControl::printers) printer.reset();
    this->log->info("-----------------------------------------------------------");
    this->log->info(" Octoprint Control Shutdown");
    this->log->info("===========================================================");
    curl_global_cleanup();
}

int App::Run() {
    if (::OctoPrintControl::config.contains("printers")) {
        if (!::OctoPrintControl::config.at("printers").is_array()) {
            this->log->error("`printers` must be an array.");
            return -1;
        }

        this->log->info("Connecting to printers...");

        for (nlohmann::json &pconf : ::OctoPrintControl::config.at("printers")) {
            try {
                std::shared_ptr<Printer> p(new Printer(pconf.at("name"), pconf.at("url"), pconf.at("apiKey")));
                ::OctoPrintControl::printers[pconf.at("id")] = p;
                p->socket->AddCallback("event", std::bind(&App::OnPrinterEvent, this, pconf.at("id").get<std::string>(), p, std::placeholders::_1, std::placeholders::_2));
            } catch(std::runtime_error &err) {
                this->log->error("Error while connecting to {}: {}", pconf.at("name").get<std::string>(), err.what());
                return -1;
            } catch (...) {
                this->log->error("Malformed printer config: {}", pconf.dump());
                continue;
            }
        }
    }

    if (::OctoPrintControl::printers.size()==0) {
        this->log->error("No printers loaded.");
        return -1;
    }

    this->log->info("Connecting to Discord gateway...");
    ::OctoPrintControl::gateway.reset(new Discord::Socket(this->token));
    ::OctoPrintControl::gateway->AddEventCallback("READY", std::bind(&App::OnReady, this, std::placeholders::_1, std::placeholders::_2));
    ::OctoPrintControl::gateway->AddEventCallback("MESSAGE_CREATE", std::bind(&App::OnNewMessage, this, std::placeholders::_1, std::placeholders::_2));
    ::OctoPrintControl::gateway->AddEventCallback("INTERACTION_CREATE", std::bind(&App::OnNewInteraction, this, std::placeholders::_1, std::placeholders::_2));

    this->running = true;
    while(this->running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        for(auto it=::OctoPrintControl::interactions.begin(); it!=::OctoPrintControl::interactions.end();) {
            if (now > it->second->expires) {
                it->second->ExpireInteraction();
                it = ::OctoPrintControl::interactions.erase(it);
            } else it++;
        }

        for (auto &[id, printer] : ::OctoPrintControl::printers) {
            if (printer->IsPrinting()) {
                if (!this->print_update_times.contains(id)) this->print_update_times[id] = now;
                std::chrono::duration<double> since_update = now - this->print_update_times[id];

                if (since_update.count() >= this->print_update_freq) {
                    this->print_update_times[id] = now;
                    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
                    std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), fmt::format("Printing Progress: {:.2f}%", printer->Progress()* 100) , 0x00FF00);
                    
                    try {
                        std::string img_type;
                        std::vector<char> img_data = printer->client->GetWebcamSnapshot(img_type);

                        std::shared_ptr<Discord::ChannelMessageAttachment> img(new Discord::ChannelMessageAttachment);
                        img->contentType = img_type;
                        img->data = img_data;
                        img->filename = "webcam.jpg";
                        msg->attachments.push_back(img);
                        em->image_url = "attachment://webcam.jpg";
                    } catch (...) {
                        this->log->warn("Couldn't get webcam snapshot.");
                    }

                    msg->embeds.push_back(em);

                    GetChannel(this->update_channel)->CreateMessage(msg);
                }
            }
        }
    }

    return 0;
}

void App::OnReady(std::string, nlohmann::json data) {
    this->user_id = data.at("user").at("id").get<std::string>();

    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed("OctoPrint Control Startup", "", 0x4B80D6);
    msg->embeds.push_back(em);
    em->fields.push_back(Discord::NewChannelMessageEmbedField("Version", "0.0.1", true));
    std::string printerlist = "";
    for (auto [name, printer]: ::OctoPrintControl::printers) printerlist += fmt::format("- `{}` ({})\n", name, printer->Name());
    em->fields.push_back(Discord::NewChannelMessageEmbedField("Printers", printerlist));

    GetChannel(this->update_channel)->CreateMessage(msg);
}

void App::OnNewMessage(std::string, nlohmann::json data) {
    std::string author_id = data.at("author").at("id").get<std::string>();

    if (author_id == this->user_id) return; // we don't care about our own messages

    std::string channel_id = data.at("channel_id").get<std::string>();
    std::string message_id = data.at("id").get<std::string>();

    if (!data.contains("content")) return;

    std::string content = data.at("content").get<std::string>();

    if (content=="") return;

    std::vector<std::string> tokens = Utils::Tokenize(content);

    if (tokens[0][0]!='!') return;

    if (::OctoPrintControl::commands.contains(tokens[0].substr(1))) {
        std::string author_name = data.at("author").at("username").get<std::string>();
        if (!this->trusted_users.contains(author_id)) {
            GetChannel(channel_id)->AddReaction(message_id, "🚫");
            this->log->warn("UNTRUSTED USER {}({}) attempted to use {}", author_name, author_id, content);
            return;
        }

        this->log->info("{}({}) -> {}", author_name, author_id, content);
        ::OctoPrintControl::commands[tokens[0].substr(1)]->Run(channel_id, message_id, author_id, std::vector<std::string>(tokens.begin() + 1, tokens.end()));
    }
}

void App::OnNewInteraction(std::string, nlohmann::json data) {
    if (data.at("type").get<int>()!=3) {
        this->log->warn("Got an interaction that wasn't from a component.");
        return;
    }

    std::string message_id = data.at("message").at("id").get<std::string>();
    std::string interaction_id = data.at("id").get<std::string>();
    std::string interaction_token = data.at("token").get<std::string>();
    std::string response = data.at("data").at("custom_id").get<std::string>();

    if (!::OctoPrintControl::interactions.contains(message_id)) {
        this->log->error("Got an interaction for a message we don't have.");
        return;
    }

    std::shared_ptr<Interactions::InteractionHandler> h = ::OctoPrintControl::interactions[message_id];
    if (h->HandleInteraction(interaction_id, interaction_token, response)) {
        ::OctoPrintControl::interactions.erase(message_id);
    }
}

void App::OnPrinterEvent(std::string printer_id, std::shared_ptr<Printer> printer, std::string, nlohmann::json data) {
    std::string event_type = data.at("type").get<std::string>();

    this->log->debug("Printer event from {}: {}, {}", printer->Name(), event_type, data.dump());

    if (event_type=="PrintStarted") {
        std::string file = data["payload"]["name"].get<std::string>();
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), "Starting Print", 0x00FF00);
        em->fields.push_back(Discord::NewChannelMessageEmbedField("File", file));
        msg->embeds.push_back(em);

        try {
            std::string img_type;
            std::vector<char> img_data = printer->client->GetWebcamSnapshot(img_type);

            std::shared_ptr<Discord::ChannelMessageAttachment> img(new Discord::ChannelMessageAttachment);
            img->contentType = img_type;
            img->data = img_data;
            img->filename = "webcam.jpg";
            msg->attachments.push_back(img);
            em->image_url = "attachment://webcam.jpg";
        } catch (...) {
            this->log->warn("Couldn't get webcam snapshot.");
        }

        GetChannel(this->update_channel)->CreateMessage(msg);

        this->print_update_times[printer_id] = std::chrono::steady_clock::now();
    } else if (event_type=="PrintCancelled") {
        std::string file = data["payload"]["name"].get<std::string>();
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), "Print Cancelled", 0xFF0000);
        em->fields.push_back(Discord::NewChannelMessageEmbedField("File", file));
        msg->embeds.push_back(em);

        try {
            std::string img_type;
            std::vector<char> img_data = printer->client->GetWebcamSnapshot(img_type);

            std::shared_ptr<Discord::ChannelMessageAttachment> img(new Discord::ChannelMessageAttachment);
            img->contentType = img_type;
            img->data = img_data;
            img->filename = "webcam.jpg";
            msg->attachments.push_back(img);
            em->image_url = "attachment://webcam.jpg";
        } catch (...) {
            this->log->warn("Couldn't get webacm snapshot");
        }

        GetChannel(this->update_channel)->CreateMessage(msg);
        this->print_update_times.erase(printer_id);
    } else if (event_type=="PrintDone") {
        std::string file = data["payload"]["name"].get<std::string>();
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), "Print Finshed", 0x00FF00);
        em->fields.push_back(Discord::NewChannelMessageEmbedField("File", file));
        msg->embeds.push_back(em);

        try {
            std::string img_type;
            std::vector<char> img_data = printer->client->GetWebcamSnapshot(img_type);

            std::shared_ptr<Discord::ChannelMessageAttachment> img(new Discord::ChannelMessageAttachment);
            img->contentType = img_type;
            img->data = img_data;
            img->filename = "webcam.jpg";
            msg->attachments.push_back(img);
            em->image_url = "attachment://webcam.jpg";
        } catch (...) {
            this->log->warn("Couldn't get webacm snapshot");
        }

        GetChannel(this->update_channel)->CreateMessage(msg);
        this->print_update_times.erase(printer_id);
    } else if (event_type=="Connected") {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), "Connected", 0x00FF00);
        msg->embeds.push_back(em);

        GetChannel(this->update_channel)->CreateMessage(msg);
    } else if (event_type=="Disconnected") {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), "Disconnected", 0xFF0000);
        msg->embeds.push_back(em);

        GetChannel(this->update_channel)->CreateMessage(msg);
    } else if (event_type=="plugin_psucontrol_psu_state_changed") {
        bool is_on = data["payload"]["isPSUOn"].get<bool>();
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        if (is_on) {
            msg->embeds.push_back(Discord::NewChannelMessageEmbed(printer->Name(), "Power ON", 0xFFFFFF));
        } else {
            msg->embeds.push_back(Discord::NewChannelMessageEmbed(printer->Name(), "Power OFF", 0xFFFFFF));
        }

        GetChannel(this->update_channel)->CreateMessage(msg);
    }
}

}