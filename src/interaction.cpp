#include "interaction.h"

#include "octoprintcontrol.h"

namespace OctoPrintControl::Interactions {

PrinterPowerOffInteraction::PrinterPowerOffInteraction(std::shared_ptr<Printer> printer, std::string channel, std::string message, std::string reference)
:printer(printer), reference_id(reference), message_id(message), channel_id(channel) {

}

bool PrinterPowerOffInteraction::HandleInteraction(std::string id, std::string token, std::string response) {
    Discord::Interaction i(config.at("token").get<std::string>(), id);

    i.CreateResponse(token, 6); // ack but don't do anything else yet
    std::shared_ptr<Discord::Channel> c = GetChannel(this->channel_id);

    if (response=="confirm") {
        try {
            this->printer->PowerOff();
        } catch (std::runtime_error &err) {
            std::shared_ptr<Discord::ChannelMessage> msg = Discord::NewChannelMessage();
            msg->content = "Couldn't turn PSU off:\n```\n";
            msg->content += err.what();
            msg->content += "\n```";
            msg->reference_message = this->reference_id;
            c->CreateMessage(msg);
            return false;
        }
        c->AddReaction(this->reference_id, "🔌");
    } else {
        c->AddReaction(this->reference_id, "❌");
    }
    
    c->DeleteMessage(this->message_id);

    return true;
}

void PrinterPowerOffInteraction::ExpireInteraction() {
    std::shared_ptr<Discord::Channel> c = GetChannel(this->channel_id);
    c->DeleteMessage(this->message_id);
    c->AddReaction(this->reference_id, "❌");
}

}