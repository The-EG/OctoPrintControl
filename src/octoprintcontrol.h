// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <string>
#include <memory>
#include <map>
#include <nlohmann/json.hpp>

#include "command.h"
#include "interaction.h"
#include "discord.h"
#include "printer.h"

namespace OctoPrintControl {

extern nlohmann::json config;

extern std::map<std::string, std::shared_ptr<Printer>> printers;

extern std::shared_ptr<Discord::Socket> gateway;

std::shared_ptr<Discord::Channel> GetChannel(std::string channel_id);

extern std::map<std::string, std::shared_ptr<Commands::BotCommand>> commands;
extern std::map<std::string, std::shared_ptr<Interactions::InteractionHandler>> interactions;

void AddCommand(Commands::BotCommand *command);

}