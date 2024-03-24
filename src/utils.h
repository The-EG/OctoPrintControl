// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <string>
#include <vector>

namespace OctoPrintControl::Utils {

std::vector<std::string> Tokenize(std::string msg);

std::string Join(std::vector<std::string> tokens);

}