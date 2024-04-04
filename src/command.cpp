#include <spdlog/sinks/stdout_color_sinks.h>
#include "command.h"

#include "octoprintcontrol.h"

namespace OctoPrintControl::Commands {

static bool ValidateCommandPrinterArg(std::vector<std::string> args, std::string channel, std::string message) {
    if (args.size()!=1) {
        std::shared_ptr<Discord::ChannelMessage> msg(new Discord::ChannelMessage);
        msg->content = "❗Error: you must specify a printer.";
        msg->reference_message = message;
        GetChannel(channel)->CreateMessage(msg);
        return false;
    }

    if (!::OctoPrintControl::printers.contains(args[0])) {
        std::shared_ptr<Discord::ChannelMessage> msg(new Discord::ChannelMessage);
        msg->content = fmt::format("❗Error: `{}` not a recognized printer.", args[0]);
        msg->reference_message = message;
        GetChannel(channel)->CreateMessage(msg);
        return false;
    }

    return true;
}

void BotCommand::SetupLogger() {
    this->log = spdlog::get(fmt::format("BotCommand::{}", this->Id()));
    if (!this->log.get()) this->log = spdlog::stdout_color_mt(fmt::format("BotCommand::{}", this->Id()));
}

void Help::Run(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    msg->reference_message = message;
    msg->content = "Available commands:\n";
    for (auto [id, c] : commands) {
        msg->content += fmt::format("- `!{}` : {}\n", id, c->Description());
    }

    GetChannel(channel)->CreateMessage(msg);
}

void Ping::Run(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    std::shared_ptr<Discord::Channel> c = GetChannel(channel);

    c->AddReaction(message, "🏓");

    std::chrono::duration<double, std::milli> ping = gateway->GatewayLatency();
    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    msg->reference_message = message;
    msg->content = fmt::format("Pong!\nGateway latency: {:.2f} ms", ping.count());
    c->CreateMessage(msg);
}

void ListPrinters::Run(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
    msg->content = "I know about the following printers:\n";
    for (std::string p : std::views::keys(::OctoPrintControl::printers)) {
        msg->content += fmt::format("- `{}` ({}) \n", p, ::OctoPrintControl::printers[p]->Name());
    }
    msg->reference_message = message;
    GetChannel(channel)->CreateMessage(msg);
}

void PowerOn::Run(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    if (!ValidateCommandPrinterArg(args, channel, message)) return;

    bool isOn = false;
    try {
        isOn = ::OctoPrintControl::printers[args[0]]->IsOn();
    } catch (std::runtime_error &err) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = "Couldn't get current PSU state:\n```\n";
        msg->content += err.what();
        msg->content += "\n```";
        msg->reference_message = message;
        GetChannel(channel)->CreateMessage(msg);
        return;
    }

    if (isOn) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = fmt::format("{} is already on!", ::OctoPrintControl::printers[args[0]]->Name());
        msg->reference_message = message;
        GetChannel(channel)->CreateMessage(msg);
        return;
    }
    
    try {
        std::thread t(&Printer::PowerOn, ::OctoPrintControl::printers[args[0]]);
        t.detach();
    } catch (std::runtime_error &err) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = "Couldn't turn PSU on:\n```\n";
        msg->content += err.what();
        msg->content += "\n```";
        msg->reference_message = message;
        GetChannel(channel)->CreateMessage(msg);
        return;
    }

    GetChannel(channel)->AddReaction(message, "🔌");
}

void PowerOff::Run(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    if (!ValidateCommandPrinterArg(args, channel, message)) return;

    std::shared_ptr<Printer> p = ::OctoPrintControl::printers[args[0]];

    bool isOn = false;
    try {
        isOn = p->IsOn();
    } catch (std::runtime_error &err) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = "Couldn't get current PSU state:\n```\n";
        msg->content += err.what();
        msg->content += "\n```";
        msg->reference_message = message;
        GetChannel(channel)->CreateMessage(msg);
        return;
    }

    if (!isOn) {
        std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
        msg->content = fmt::format("{} is already off!", p->Name());
        msg->reference_message = message;
        GetChannel(channel)->CreateMessage(msg);
        return;
    }

    std::shared_ptr<Discord::ChannelMessage> msg(new Discord::ChannelMessage);
    msg->content = fmt::format("⚠️ CONFIRM: Power off {}?", args[0]);
    msg->reference_message = message;
    std::shared_ptr<Discord::ActionRowComponent> row(new Discord::ActionRowComponent);
    row->AddComponent(std::shared_ptr<Discord::ButtonComponent>(new Discord::ButtonComponent(1, "Cancel", "cancel")));
    row->AddComponent(std::shared_ptr<Discord::ButtonComponent>(new Discord::ButtonComponent(4, "Confirm", "confirm")));

    msg->components.push_back(row);

    GetChannel(channel)->CreateMessage(msg);

    if (msg->id.size()==0) {
        this->log->error("Couldn't create message for power off.");
        return;
    }

    std::shared_ptr<Interactions::PrinterPowerOffInteraction> pi(new Interactions::PrinterPowerOffInteraction(p, channel, msg->id, message));
    pi->expires = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    ::OctoPrintControl::interactions[msg->id] = pi;
}

void PrinterStatus::Run(std::string channel, std::string message, std::string author, std::vector<std::string> args) {
    if (!ValidateCommandPrinterArg(args, channel, message)) return;

    GetChannel(channel)->TriggerTyping();

    std::shared_ptr<Printer> p = ::OctoPrintControl::printers[args[0]];

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

    GetChannel(channel)->CreateMessage(msg);
}

}