#include "octoprintcontrol.h"

namespace OctoPrintControl {

nlohmann::json config;

std::map<std::string, std::shared_ptr<Printer>> printers;

std::shared_ptr<Discord::Socket> gateway;

std::map<std::string, std::shared_ptr<Commands::BotCommand>> commands;

std::map<std::string, std::shared_ptr<Interactions::InteractionHandler>> interactions;

static std::map<std::string, std::shared_ptr<Discord::Channel>> channel_cache;

std::shared_ptr<Discord::Channel> GetChannel(std::string channel_id) {
    if (!channel_cache.contains(channel_id)) {
        std::string token = config.at("token").get<std::string>();
        channel_cache[channel_id] = std::shared_ptr<Discord::Channel>(new Discord::Channel(token, channel_id));
    }
    return channel_cache[channel_id];
}

void AddCommand(Commands::BotCommand *command) {
    std::shared_ptr<Commands::BotCommand> p(command);
    ::OctoPrintControl::commands[p->Id()] = p;
}

}