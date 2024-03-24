// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include "utils.h"

namespace OctoPrintControl::Utils {

std::vector<std::string> Tokenize(std::string msg) {
    std::vector<std::string> tokens;

    std::string balance = msg;

    while(balance.find_first_of(' ')!=std::string::npos) {
        std::string t = balance.substr(0, balance.find_first_of(' '));
        balance = balance.substr(balance.find_first_of(' ')+1);
        tokens.push_back(t);
    }

    if (balance.size()) tokens.push_back(balance);

    return tokens;
}

std::string Join(std::vector<std::string> tokens) {
    std::string str;

    if (tokens.size()==0) return str;

    size_t i = 0;
    do {
        if (str.size()) str += ' ';
        str += tokens[i++];
    } while(i<tokens.size());

    return str;
}

}