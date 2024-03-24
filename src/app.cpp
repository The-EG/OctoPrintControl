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
    this->log->info(" Version: 0.0.1");
    this->log->info("-----------------------------------------------------------");

    Magick::InitializeMagick(argv[0]);

    std::filesystem::path conf_path = std::filesystem::current_path() / "OctoPrintControl.json";
    if (argc==2) {
        conf_path = argv[1];
    }

    this->log->info("Loading configuration from {}", conf_path.string());

    try {
        std::ifstream confstream(conf_path);
        this->config = nlohmann::json::parse(confstream);
    } catch (nlohmann::json::parse_error &err) {
        this->log->critical("Couldn't parse config: {}", err.what());
        exit(-1);
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    this->commands["ping"] = BIND_COMMAND(App::CommandPing);
    this->commands["list-printers"] = BIND_COMMAND(App::CommandListPrinters);
    this->commands["power-on"] = BIND_COMMAND(App::CommandPowerOn);
    this->commands["power-off"] = BIND_COMMAND(App::CommandPowerOff);
    this->commands["printer-status"] = BIND_COMMAND(App::CommandPrinterStatus);

    try {
        this->config.at("updateChannel").get_to(this->update_channel);
        this->log->info("Update Channel: {}", this->update_channel);
    } catch (...) {
        this->log->critical("No updateChannel specified in config.");
        exit(-1);
    }

    try {
        for (nlohmann::json &id : this->config.at("trustedUsers")) {
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
        this->config.at("printUpdateFreq").get_to(this->print_update_freq);
    } catch(...) {
        this->log->warn("No printUpdateFreq in config, using default.");
        this->print_update_freq = 600;
    }
    this->log->info("Print Update Message Frequency: {} seconds", this->print_update_freq);
}

App::~App() {
    this->gateway.reset();
    for (auto &[id, printer] : this->printers) printer.reset();
    this->log->info("-----------------------------------------------------------");
    this->log->info(" Octoprint Control Shutdown");
    this->log->info("===========================================================");
    curl_global_cleanup();
}

int App::Run() {
    try {
        this->config.at("token").get_to(this->token);
    } catch (...) {
        this->log->error("Config must have a string value for `token`.");
        return -1;
    }

    if (this->config.contains("printers")) {
        if (!this->config.at("printers").is_array()) {
            this->log->error("`printers` must be an array.");
            return -1;
        }

        this->log->info("Connecting to printers...");

        for (nlohmann::json &pconf : this->config.at("printers")) {
            try {
                std::shared_ptr<Printer> p(new Printer(pconf.at("name"), pconf.at("url"), pconf.at("apiKey")));
                this->printers[pconf.at("id")] = p;
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

    if (this->printers.size()==0) {
        this->log->error("No printers loaded.");
        return -1;
    }

    this->log->info("Start");

    this->log->info("Connecting to Discord gateway...");
    this->gateway.reset(new Discord::Socket(this->token));
    this->gateway->AddEventCallback("READY", std::bind(&App::OnReady, this, std::placeholders::_1, std::placeholders::_2));
    this->gateway->AddEventCallback("MESSAGE_CREATE", std::bind(&App::OnNewMessage, this, std::placeholders::_1, std::placeholders::_2));
    this->gateway->AddEventCallback("INTERACTION_CREATE", std::bind(&App::OnNewInteraction, this, std::placeholders::_1, std::placeholders::_2));

    this->running = true;
    while(this->running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        for(auto it=this->interactions.begin(); it!=this->interactions.end();) {
            if (now > it->second->expires) {
                it->second->ExpireInteraction();
                it = this->interactions.erase(it);
            } else it++;
        }

        for (auto &[id, printer] : this->printers) {
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

                    this->GetChannel(this->update_channel)->CreateMessage(msg);
                }
            }
        }
    }

    this->log->info("End");

    return 0;
}

void App::OnReady(std::string, nlohmann::json data) {
    this->user_id = data.at("user").at("id").get<std::string>();

    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed("OctoPrint Control Startup", "", 0x4B80D6);
    msg->embeds.push_back(em);
    em->fields.push_back(Discord::NewChannelMessageEmbedField("Version", "0.0.1", true));
    std::string printerlist = "";
    for (auto [name, printer]: this->printers) printerlist += fmt::format("- `{}` ({})\n", name, printer->Name());
    em->fields.push_back(Discord::NewChannelMessageEmbedField("Printers", printerlist));

    this->GetChannel(this->update_channel)->CreateMessage(msg);
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

    if (this->commands.contains(tokens[0].substr(1))) {
        std::string author_name = data.at("author").at("username").get<std::string>();
        if (!this->trusted_users.contains(author_id)) {
            this->GetChannel(channel_id)->AddReaction(message_id, "🚫");
            this->log->warn("UNTRUSTED USER {}({}) attempted to use {}", author_name, author_id, content);
            return;
        }

        this->log->info("{}({}) -> {}", author_name, author_id, content);
        this->commands[tokens[0].substr(1)](channel_id, message_id, author_id, std::vector<std::string>(tokens.begin() + 1, tokens.end()));
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

    if (!this->interactions.contains(message_id)) {
        this->log->error("Got an interaction for a message we don't have.");
        return;
    }

    std::shared_ptr<InteractionHandler> h = this->interactions[message_id];
    if (h->HandleInteraction(interaction_id, interaction_token, response)) {
        this->interactions.erase(message_id);
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

        this->GetChannel(this->update_channel)->CreateMessage(msg);

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

        this->GetChannel(this->update_channel)->CreateMessage(msg);
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

        this->GetChannel(this->update_channel)->CreateMessage(msg);
        this->print_update_times.erase(printer_id);
    } else if (event_type=="Connected") {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), "Connected", 0x00FF00);
        msg->embeds.push_back(em);

        this->GetChannel(this->update_channel)->CreateMessage(msg);
    } else if (event_type=="Disconnected") {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        std::shared_ptr<Discord::ChannelMessageEmbed> em = Discord::NewChannelMessageEmbed(printer->Name(), "Disconnected", 0xFF0000);
        msg->embeds.push_back(em);

        this->GetChannel(this->update_channel)->CreateMessage(msg);
    } else if (event_type=="plugin_psucontrol_psu_state_changed") {
        bool is_on = data["payload"]["isPSUOn"].get<bool>();
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        if (is_on) {
            msg->embeds.push_back(Discord::NewChannelMessageEmbed(printer->Name(), "Power ON", 0xFFFFFF));
        } else {
            msg->embeds.push_back(Discord::NewChannelMessageEmbed(printer->Name(), "Power OFF", 0xFFFFFF));
        }

        this->GetChannel(this->update_channel)->CreateMessage(msg);
    }
}

void App::CommandPing(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    this->GetChannel(channel)->AddReaction(message, "🏓");

    std::chrono::duration<double, std::milli> ping = this->gateway->GatewayLatency();
    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    msg->reference_message = message;
    msg->content = fmt::format("Pong!\nGateway latency: {:.2f} ms", ping.count());
    this->GetChannel(channel)->CreateMessage(msg);
}

void App::CommandListPrinters(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    msg->content = "I know about the following printers:\n";
    for (std::string p : std::views::keys(this->printers)) {
        msg->content += fmt::format("- `{}` ({}) \n", p, this->printers[p]->Name());
    }
    msg->reference_message = message;
    this->GetChannel(channel)->CreateMessage(msg);
}

void App::CommandPowerOn(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    if (!this->ValidateCommandPrinterArg(args, channel, message)) return;

    bool isOn = false;
    try {
        isOn = this->printers[args[0]]->IsOn();
    } catch (std::runtime_error &err) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = "Couldn't get current PSU state:\n```\n";
        msg->content += err.what();
        msg->content += "\n```";
        msg->reference_message = message;
        this->GetChannel(channel)->CreateMessage(msg);
        return;
    }

    if (isOn) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = fmt::format("{} is already on!", this->printers[args[0]]->Name());
        msg->reference_message = message;
        this->GetChannel(channel)->CreateMessage(msg);
        return;
    }
    
    try {
        std::thread t(&Printer::PowerOn, this->printers[args[0]]);
        t.detach();
    } catch (std::runtime_error &err) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = "Couldn't turn PSU on:\n```\n";
        msg->content += err.what();
        msg->content += "\n```";
        msg->reference_message = message;
        this->GetChannel(channel)->CreateMessage(msg);
        return;
    }

    this->GetChannel(channel)->AddReaction(message, "🔌");
}

void App::CommandPowerOff(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    if (!this->ValidateCommandPrinterArg(args, channel, message)) return;

    std::shared_ptr<Printer> p = this->printers[args[0]];

    bool isOn = false;
    try {
        isOn = p->IsOn();
    } catch (std::runtime_error &err) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = "Couldn't get current PSU state:\n```\n";
        msg->content += err.what();
        msg->content += "\n```";
        msg->reference_message = message;
        this->GetChannel(channel)->CreateMessage(msg);
        return;
    }

    if (!isOn) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = fmt::format("{} is already off!", p->Name());
        msg->reference_message = message;
        this->GetChannel(channel)->CreateMessage(msg);
        return;
    }

    std::shared_ptr<Discord::ChannelMessage> msg(new Discord::ChannelMessage);
    msg->content = fmt::format("⚠️ CONFIRM: Power off {}?", args[0]);
    msg->reference_message = message;
    std::shared_ptr<Discord::ActionRowComponent> row(new Discord::ActionRowComponent);
    row->AddComponent(std::shared_ptr<Discord::ButtonComponent>(new Discord::ButtonComponent(1, "Cancel", "cancel")));
    row->AddComponent(std::shared_ptr<Discord::ButtonComponent>(new Discord::ButtonComponent(4, "Confirm", "confirm")));

    msg->components.push_back(row);

    this->GetChannel(channel)->CreateMessage(msg);

    if (msg->id.size()==0) {
        this->log->error("Couldn't create message for power off.");
        return;
    }

    std::shared_ptr<PrinterPowerOffInteraction> pi(new PrinterPowerOffInteraction(this->token, p, channel, msg->id, message));
    pi->expires = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    this->interactions[msg->id] = pi;
}

void App::CommandPrinterStatus(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    if (!this->ValidateCommandPrinterArg(args, channel, message)) return;

    this->GetChannel(channel)->TriggerTyping();

    std::shared_ptr<Printer> p = this->printers[args[0]];

    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    msg->reference_message = message;
    std::shared_ptr<Discord::ChannelMessageEmbed> e = Discord::NewChannelMessageEmbed(p->Name());

    try {
        std::string img_type;
        std::vector<char> img_data = p->client->GetWebcamSnapshot(img_type);

        std::shared_ptr<Discord::ChannelMessageAttachment> img(new Discord::ChannelMessageAttachment);
        img->contentType = img_type;
        img->data = img_data;
        img->filename = "webcam.jpg";
        msg->attachments.push_back(img);
        e->image_url = "attachment://webcam.jpg";
    } catch (...) {
        this->log->warn("Couldn't get webacm snapshot");
    }    

    e->fields.push_back(Discord::NewChannelMessageEmbedField("Status", p->StatusText(), true));
    if (p->IsConnected()) {
        if (p->FileDisplay().size()) e->fields.push_back(Discord::NewChannelMessageEmbedField("File", p->FileDisplay(), true));

        std::string temps = "```\n";
        for (auto &[k, v] : p->last_temps) {
            temps = temps + fmt::format("{: <6} : {:6.2f}°", k, v->actual);
            if (v->target>0) temps += fmt::format(" / {:6.2f}°", v->target);
            temps += "\n";
        }
        temps += "```\n";

        e->fields.push_back(Discord::NewChannelMessageEmbedField("Temperatures", temps, false));
    }
    msg->embeds.push_back(e);

    this->GetChannel(channel)->CreateMessage(msg);
}

bool App::ValidateCommandPrinterArg(std::vector<std::string> args, std::string channel, std::string message) {
    if (args.size()!=1) {
        std::shared_ptr<Discord::ChannelMessage> msg(new Discord::ChannelMessage);
        msg->content = "❗Error: you must specify a printer.";
        msg->reference_message = message;
        this->GetChannel(channel)->CreateMessage(msg);
        return false;
    }

    if (!this->printers.contains(args[0])) {
        std::shared_ptr<Discord::ChannelMessage> msg(new Discord::ChannelMessage);
        msg->content = fmt::format("❗Error: `{}` not a recognized printer.", args[0]);
        msg->reference_message = message;
        this->GetChannel(channel)->CreateMessage(msg);
        return false;
    }

    return true;
}

std::shared_ptr<Discord::Channel> App::GetChannel(std::string channel_id) {
    if (!this->channel_cache.contains(channel_id))
        this->channel_cache[channel_id] = std::shared_ptr<Discord::Channel>(new Discord::Channel(this->token, channel_id));
    return this->channel_cache[channel_id];
}

PrinterPowerOffInteraction::PrinterPowerOffInteraction(std::string token, std::shared_ptr<Printer> printer, std::string channel, std::string message, std::string reference)
:InteractionHandler(token), printer(printer), reference_id(reference), message_id(message), channel_id(channel) {

}

bool PrinterPowerOffInteraction::HandleInteraction(std::string id, std::string token, std::string response) {
    Discord::Interaction i(this->bot_token, id);

    i.CreateResponse(token, 6); // ack but don't do anything else yet
    Discord::Channel c(this->bot_token, this->channel_id);

    if (response=="confirm") {
        try {
            this->printer->PowerOff();
        } catch (std::runtime_error &err) {
            std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
            msg->content = "Couldn't turn PSU off:\n```\n";
            msg->content += err.what();
            msg->content += "\n```";
            msg->reference_message = this->reference_id;
            c.CreateMessage(msg);
            return false;
        }
        c.AddReaction(this->reference_id, "🔌");
    } else {
        c.AddReaction(this->reference_id, "❌");
    }
    
    c.DeleteMessage(this->message_id);

    return true;
}

void PrinterPowerOffInteraction::ExpireInteraction() {
    Discord::Channel c(this->bot_token, this->channel_id);
    c.DeleteMessage(this->message_id);
    c.AddReaction(this->reference_id, "❌");
}

}