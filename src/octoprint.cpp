// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include "octoprint.h"
#include <stdexcept>
#include <chrono>
#include <vector>
#include <random>
#include <fmt/core.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <Magick++.h>

namespace OctoPrintControl::OctoPrint {

Client::Client(std::string name, std::string url, std::string apikey)
:name(name), url(url), apikey(apikey) {
    this->log = spdlog::get(fmt::format("OctoPrint::Client::{}", name));
    if (!this->log.get()) this->log = spdlog::stdout_color_mt(fmt::format("OctoPrint::Client::{}", name));

    this->http.reset(new HTTP::Client());
    this->http->AddHeader(fmt::format("X-Api-Key: {}", this->apikey));
}

Client::~Client() {
    
}

nlohmann::json Client::PassiveLogin() {
    nlohmann::json reqData = {
        { "passive", true }
    };

    std::shared_ptr<HTTP::Request> req(new HTTP::Request);
    req->method = HTTP::RequestMethod::POST;
    req->url = this->url + "/api/login";
    req->body.reset(new HTTP::JSONRequestData(reqData));
    
    std::shared_ptr<HTTP::Response> resp = this->http->Perform(req);

    if (resp->code!=200) throw std::runtime_error("Couldn't authenticate");
    if (resp->contentType!="application/json") throw std::runtime_error("Expected JSON");

    try {
        nlohmann::json session = nlohmann::json::parse(std::string(resp->body.begin(), resp->body.end()));
        return session;
    } catch (nlohmann::json::parse_error&) {
        throw std::runtime_error("Couldn't parse response.");
    }
}

nlohmann::json Client::PluginSimpleApiCommand(std::string plugin, nlohmann::json data) {
    std::shared_ptr<HTTP::Request> req(new HTTP::Request);
    req->method = HTTP::RequestMethod::POST;
    req->url = this->url + "/api/plugin/" + plugin;
    req->body.reset(new HTTP::JSONRequestData(data));

    std::shared_ptr<HTTP::Response> resp = this->http->Perform(req);

    if (resp->code==200) {
        if (resp->contentType!="application/json") throw std::runtime_error("Expected JSON");

        try {
            nlohmann::json session = nlohmann::json::parse(std::string(resp->body.begin(), resp->body.end()));
            return session;
        } catch (nlohmann::json::parse_error&) {
            throw std::runtime_error("Couldn't parse response.");
        }
    } else if (resp->code==204) return nlohmann::json();
    else {
        throw std::runtime_error(fmt::format("Error while performing simple api command: {}", resp->code));
    }
}

std::vector<char> Client::GetWebcamSnapshot(std::string &imageType) {
    std::string settingsStr;
    std::string contentType;

    std::shared_ptr<HTTP::Request> settingsReq = HTTP::NewGetRequest(this->url + "/api/settings");
    std::shared_ptr<HTTP::Response> settingsResp = this->http->Perform(settingsReq);

    if (settingsResp->code!=200) {
        throw std::runtime_error("Couldn't retrieve webcam settings.");
    }

    if (settingsResp->contentType!="application/json") {
        throw std::runtime_error("Settings not returned as application/json.");
    }

    nlohmann::json settings;
    try {
        settings = nlohmann::json::parse(std::string(settingsResp->body.begin(), settingsResp->body.end()));
    } catch (nlohmann::json::parse_error &) {
        throw std::runtime_error("Couldn't parse settings json.");
    }

    std::string snapshotURL;
    try {
        snapshotURL = settings.at("webcam").at("snapshotUrl");
    } catch (nlohmann::json::type_error&) {
        throw std::runtime_error("Invalid settings json.");
    }

    bool flipV = settings.at("webcam").at("flipV").get<bool>();
    bool flipH = settings.at("webcam").at("flipH").get<bool>();

    std::shared_ptr<HTTP::Request> req = HTTP::NewGetRequest(snapshotURL);
    std::shared_ptr<HTTP::Response> resp = this->http->Perform(req);

    if (resp->code!=200) {
        throw std::runtime_error("Couldn't retrieve snapshot image.");
    }

    std::vector<char> retData(resp->body.begin(), resp->body.end());
    //for (char c : resp->body) retData.push_back(c);

    if (flipH || flipV) {
        Magick::Blob blob(retData.data(), retData.size());
        Magick::Image img(blob);
        if (flipV) img.flip();
        if (flipH) img.flop();

        img.write(&blob);
        retData.clear();
        const char *bdata = static_cast<const char*>(blob.data());
        for (size_t i=0;i<blob.length();i++) {
            retData.push_back(bdata[i]);
        }
    }

    imageType = contentType;

    return retData;
}

Socket::Socket(std::string url) {
    this->baseurl = url;
    this->log = spdlog::get("OctoPrint::Socket::" + url);
    if (!this->log.get()) this->log = spdlog::stdout_color_mt("OctoPrint::Socket::" + url);

    
}

Socket::~Socket() {
    this->websocket->Disconnect();
}

void Socket::Connect() {
    int serverCode;
    std::string sessionCode;
    std::default_random_engine rand;
    std::uniform_int_distribution<int> serverDist(1,999);
    std::uniform_int_distribution<int> sessionDistChar((int)'a',(int)'z');
    std::uniform_int_distribution<int> keyDistChar(32,127);

    rand.seed(std::chrono::high_resolution_clock::now().time_since_epoch().count());

    serverCode = serverDist(rand);
    for(int i=0;i<16;i++) sessionCode.push_back((char)sessionDistChar(rand));

    std::string fullUrl = this->baseurl + "/sockjs/" + std::to_string(serverCode) + "/" + sessionCode + "/websocket";

    this->websocket.reset(new Websocket::Client(fullUrl));
    this->websocket->AddDataReceivedCallback(std::bind(&Socket::OnWebsocketData, this, std::placeholders::_1));
    
    while (true) {
        try {
            this->websocket->Connect();
            break;
        } catch (std::runtime_error &err) {
            this->log->error("Error while connecting, retrying in 30 seconds: {}", err.what());
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }        
}

void Socket::WatchdogMain() {
    this->last_hb = std::chrono::steady_clock::now();

    while (true) {
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        std::chrono::duration<double> dur = now - this->last_hb;

        if (dur.count() >= 45.0) {
            this->log->warn("Watchdog triggered, attempting to reconnect");
            this->Connect();
            return;
        }

        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

void Socket::OnWebsocketData(std::vector<char> data) {
    std::vector<char> d = data;

    if (d[0]=='o') {
        //this->log->info("Websocket open");
        std::thread t(&Socket::WatchdogMain, this);
        t.detach();
        if (d.size()==1) return;
        d = std::vector<char>(data.begin()+1, data.end());
    }

    if (d.front()=='h' || d.back()=='h') {
        //this->log->info("Websocket heartbeat <3");
        this->last_hb = std::chrono::steady_clock::now();
        if (d.size()==1) return;
        if (d.front()=='h') d = std::vector<char>(data.begin()+1, data.end());
        if (d.back()=='h') d = std::vector<char>(data.begin(), data.end()-1);
    }

    switch(d[0]) {
    case 'a':
        this->ProcessMessageArray(d);
        break;
    default:
        this->log->error("Unknown message from websocket: {}", std::string(data.begin(), data.end()));
        break;
    } 
}

void Socket::ProcessMessageArray(std::vector<char> data) {
    std::string datastr(data.begin(), data.end());

    do {
        size_t nexta = datastr.find("a[", 1);
        std::string message;
        if (nexta==std::string::npos) nexta = datastr.length();
        message = std::string(datastr.begin() + 1, datastr.begin() + nexta);
        if (nexta >= datastr.length()) datastr = "";
        else datastr = std::string(datastr.begin() + nexta, datastr.end());

        nlohmann::json messages;
        try {
            messages = nlohmann::json::parse(message);
        } catch (nlohmann::json::parse_error& err) {
            this->log->error("Couldn't parse message json: {},\n{}", message, err.what());
            return;
        }

        if(!messages.is_array()) { this->log->error("Websocket message not array"); }
        else {
            for(nlohmann::json m : messages) {
                for(auto &[key, value] : m.items()) {
                    if (this->callbacks.find(key)!=this->callbacks.end()) {
                        for (SocketDataCallback &cb : this->callbacks[key]) {
                            try {
                                cb(key, value);
                            } catch(...) {
                                this->log->error("Exception while processing {} callback, data = {}", key, value.dump());
                            }
                        }
                    }
                }
            }
        }
    } while (datastr.size());
}

void Socket::AddCallback(std::string event, SocketDataCallback callback) {
    if (!this->callbacks.contains(event)) this->callbacks[event] = std::list<SocketDataCallback>();
    this->callbacks[event].push_back(callback);
}

void Socket::Send(nlohmann::json data) {
    nlohmann::json msgarr = nlohmann::json::array({data.dump()});
    this->websocket->Send(msgarr.dump());
}

}