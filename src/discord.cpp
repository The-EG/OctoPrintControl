// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#include "discord.h"
#include <fmt/core.h>
#include <random>
#include <chrono>
#include <spdlog/sinks/stdout_color_sinks.h>

static const char *const BASE_URL = "https://discord.com/api/v10";
static const char *const USER_AGENT = "DiscordBot (https://github.com/The-EG/OctoPrintControl, 0.0.1)";

namespace OctoPrintControl::Discord {

RESTClient::RESTClient(std::string token) 
:token(token) {
    this->log = spdlog::get("Discord::RESTClient");
    if (!this->log.get()) this->log = spdlog::stdout_color_mt("Discord::RESTClient");

    this->client.reset(new HTTP::Client(USER_AGENT));
    this->client->AddHeader(fmt::format("Authorization: Bot {}", this->token));
}

RESTClient::~RESTClient() {
}

nlohmann::json ActionRowComponent::ToJSON() {
    nlohmann::json comps = nlohmann::json::array();
    for (std::shared_ptr<ChannelMessageComponent> c : this->components) {
        comps.push_back(c->ToJSON());
    }

    return {
        { "type", 1 },
        { "components", comps }
    };
}

void ActionRowComponent::AddComponent(std::shared_ptr<ChannelMessageComponent> component) {
    this->components.push_back(component);
}

nlohmann::json ButtonComponent::ToJSON() {
    return {
        { "type", 2 },
        { "style", this->style },
        { "label", this->label },
        { "custom_id", this->id }
    };
}

std::shared_ptr<HTTP::MultiPartRequestData> ChannelMessage::ToMultiPart() {
     nlohmann::json payload_data = {
        { "content", this->content },
        { "components", nlohmann::json::array() },
        { "attachments", nlohmann::json::array() },
        { "embeds", nlohmann::json::array() }
    };

    if (this->reference_message.size() > 0) {
        payload_data["message_reference"] = {
            { "message_id", this->reference_message }
        };
    }

    for (std::shared_ptr<ChannelMessageComponent> c : this->components) {
        payload_data["components"].push_back(c->ToJSON());
        }

    for (size_t i=0;i<this->attachments.size();i++) {
        payload_data["attachments"].push_back({
            { "id", i },
            { "description", this->attachments[i]->description },
            { "filename", this->attachments[i]->filename }
        });
    }

    for (std::shared_ptr<ChannelMessageEmbed> e : this->embeds) {
        nlohmann::json embed = {
            { "type", "rich" },
            { "color", e->color }
        };
        if (e->title.size()) embed["title"] = e->title;
        if (e->description.size()) embed["description"] = e->description;
        if (e->fields.size()) {
            embed["fields"] = nlohmann::json::array();
            for (std::shared_ptr<ChannelMessageEmbedField> f : e->fields) {
                nlohmann::json field = {
                    { "name", f->name },
                    { "value", f->value },
                    { "inline", f->isinline }
                };
                embed["fields"].push_back(field);
            }
        }
        if (e->image_url.size()) embed["image"] = nlohmann::json({ { "url", e->image_url } });
        payload_data["embeds"].push_back(embed);
    }

    std::shared_ptr<HTTP::MultiPartRequestData> mp(new HTTP::MultiPartRequestData);
    std::string payloadstr = payload_data.dump();
    mp->AddPart("payload_json", payloadstr.c_str());
    for (size_t i=0; i<this->attachments.size(); i++) {
        mp->AddFile(fmt::format("files[{}]", i), this->attachments[i]->filename, this->attachments[i]->contentType, this->attachments[i]->data);
    }

    return mp;
}

Channel::Channel(std::string token, std::string id) 
:RESTClient(token), id(id) {
    this->log = spdlog::get("Discord.Channel." + this->id);
    if (!this->log.get()) this->log = spdlog::stdout_color_mt("Discord.Channel." + this->id);
}

void Channel::CreateMessage(std::shared_ptr<ChannelMessage> message) {
    std::string endpoint = fmt::format("/channels/{}/messages", this->id);
    
    std::shared_ptr<HTTP::Request> req(new HTTP::Request);
    req->url = BASE_URL + endpoint;
    req->method = HTTP::RequestMethod::POST;
    req->body = message->ToMultiPart();

    std::shared_ptr<HTTP::Response> resp = this->client->Perform(req);

    if (resp->code!=200) {
        this->log->warn("Couldn't create message: {}", std::string(resp->body.begin(), resp->body.end()));
        return;
    }

    if (resp->contentType!="application/json") {
        this->log->error("Create message response isn't json.");
        return;
    }

    nlohmann::json data = nlohmann::json::parse(resp->body);
    message->id = data["id"].get<std::string>();
}

void Channel::EditMessage(std::shared_ptr<ChannelMessage> message) {
    if (message->id.size()==0) {
        this->log->error("Can't edit message without an id.");
        return;
    }

    std::string endpoint = fmt::format("/channels/{}/messages/{}", this->id, message->id);

    std::shared_ptr<HTTP::Request> req(new HTTP::Request);
    req->url = BASE_URL + endpoint;
    req->method = HTTP::RequestMethod::PATCH;
    req->body = message->ToMultiPart();

    std::shared_ptr<HTTP::Response> resp = this->client->Perform(req);

    if (resp->code!=200) {
        this->log->warn("Couldn't edit message.");
    }
}

void Channel::DeleteMessage(std::string id) {
    std::string endpoint = fmt::format("/channels/{}/messages/{}", this->id, id);
    
    std::shared_ptr<HTTP::Request> req(new HTTP::Request);
    req->method = HTTP::RequestMethod::DELETE;
    req->url = BASE_URL + endpoint;

    std::shared_ptr<HTTP::Response> resp = this->client->Perform(req);

    if (resp->code!=204) {
        this->log->warn("Couldn't delete message.");
    }
}

void Channel::AddReaction(std::string message, std::string reaction) {
    std::string endpoint = fmt::format("/channels/{}/messages/{}/reactions/{}/@me", this->id, message, this->client->EscapeString(reaction));

    std::shared_ptr<HTTP::Request> req = HTTP::NewPutRequest(BASE_URL + endpoint);
    std::shared_ptr<HTTP::Response> resp = this->client->Perform(req);
}

void Channel::TriggerTyping() {
    std::string endpoint = fmt::format("/channels/{}/typing", this->id);

    std::shared_ptr<HTTP::Request> req(new HTTP::Request);
    req->url = BASE_URL + endpoint;
    req->method = HTTP::RequestMethod::POST;
    
    std::shared_ptr<HTTP::Response> resp = this->client->Perform(req);
}

Socket::Socket(std::string token)
:token(token) {
    this->log = spdlog::get("Discord::Socket");
    if (!this->log) this->log = spdlog::stdout_color_mt("Discord::Socket");

    this->AddEventCallback("READY", std::bind(&Socket::ProcessReadyEvent, this, std::placeholders::_1, std::placeholders::_2));

    this->http.reset(new HTTP::Client(USER_AGENT));
    
    this->Reconnect();
}

Socket::~Socket() {
    this->log->debug("Shutting down gateway socket");
    this->runHB = false;
    if (this->hbThread.joinable()) this->hbThread.join();
    this->websocket.reset();
}

void Socket::AddEventCallback(std::string event, SocketEventCallback callback) {
    if (!this->event_callbacks.count(event)) this->event_callbacks[event] = std::list<SocketEventCallback>();

    this->event_callbacks[event].push_back(callback);
}

void Socket::GetGatewayURL() {
    std::shared_ptr<HTTP::Request> req = HTTP::NewGetRequest(BASE_URL + std::string("/gateway"));
    std::shared_ptr<HTTP::Response> resp = this->http->Perform(req);

    if (!(200 <= resp->code && resp->code < 300)) {
        this->log->error("Error while retrieving Discord websocket gateway URL: {}", std::string(resp->body.begin(), resp->body.end()));
        return;
    }

    nlohmann::json respjson = nlohmann::json::parse(std::string(resp->body.begin(), resp->body.end()));
    try {
        this->ws_url = respjson.at("url").get<std::string>();
    } catch (nlohmann::json::out_of_range &) {
        this->log->error("url key not found in response, can't get Discord websocket gateway URL.");
        return;
    }

    this->ws_url += "?v10&encoding=json";
}

void Socket::HBThreadMain() {
    this->runHB = true;
    std::default_random_engine rand;
    std::uniform_real_distribution<double> jitterDist(0, 1);
    rand.seed((unsigned int)std::chrono::high_resolution_clock::now().time_since_epoch().count());

    double jitter = jitterDist(rand);
    std::chrono::milliseconds duration((long)(this->hb_int * jitter));
    this->log->info("Starting heartbeat thread, waiting {:0.2f} seconds before sending first heartbeat.", duration.count() / 1000.0);
    std::this_thread::sleep_for(duration);
    this->haveAck = false;
    this->SendHeartbeat(this->seq);

    while (this->runHB) {
        std::chrono::duration<double, std::milli> since_last = std::chrono::steady_clock::now() - this->last_hb_sent;
        if (since_last.count() >= this->hb_int) {
            if (!this->haveAck) {
                this->log->error("Didn't get HB ack, disconnecting.");
                this->websocket->Disconnect();
                this->Reconnect(this->resume_url!="");
                break;
            } else {
                this->haveAck = false;
                this->SendHeartbeat(this->seq);
            }
        }        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));        
    }

    this->log->debug("HB Thread end");
}

void Socket::Reconnect(bool resume) {
    std::thread t(&Socket::ConnectThreadMain, this, resume);
    t.detach();
}

void Socket::ConnectThreadMain(bool resume) {
    while (this->ws_url=="") {
        try {
            this->GetGatewayURL();
        } catch (std::runtime_error &err) {
            this->log->error("Couldn't get Gateway URL, trying again in 30 seconds: {}", err.what());
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    }

    this->runHB = false;
    if (this->hbThread.joinable()) this->hbThread.join();

    if (resume) {
        this->log->info("Attempting to resume connection to {}", this->resume_url);
        this->websocket.reset(new Websocket::Client(this->resume_url));
    } else {
        this->haveID = false;
        this->resume_url = "";
        this->log->info("Attempting to (re)connect to {}", this->ws_url);
        this->websocket.reset(new Websocket::Client(this->ws_url));
    }

    this->websocket->AddDataReceivedCallback(std::bind(&Socket::OnWebsocketData, this, std::placeholders::_1));

    do {
        try {
            this->websocket->Connect();
            break;
        } catch(std::runtime_error &err) {
            this->log->error("Error while connecting, trying again in 30 seconds: {}", err.what());
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
    } while (true);

    if (resume) {
        nlohmann::json resume = {
            { "op", 6 },
            { "d", {
                { "token", this->token },
                { "session_id", this->session },
                { "seq", this->seq }
            }}
        };
        this->log->info("Resuming sessions {}", this->session);
        this->websocket->Send(resume.dump());
    } 
}

void Socket::OnWebsocketData(std::vector<char> data) {
    std::vector<char>::iterator begin = data.begin();
    std::vector<char>::iterator end = data.end();
    while(begin!=end) {
        std::string datastr(begin, end);
        nlohmann::json msg;

        try {
            msg = nlohmann::json::parse(datastr);
        } catch (nlohmann::json::parse_error &err) {
            try {
                end = begin + (err.byte - 1);
                datastr = std::string(begin, end);
                msg = nlohmann::json::parse(datastr);
            } catch (nlohmann::json::parse_error& err2) {
                this->log->error("Couldn't parse gateway message: {} - {}", err2.what(), datastr);
                return;
            }
        }

        begin = end;
        end = data.end();
        if (msg.contains("s") && !msg.at("s").is_null()) this->seq = msg.at("s").get<int64_t>();

        switch(msg.at("op").get<int>()) {
        case 0: // event dispatch
            this->DispatchEvent(msg);
            break;
        case 1: // hb
            this->SendHeartbeat(this->seq);
            haveAck = false;
            break;
        case 7: // reconnect
            this->log->info("Got reconnect message.");
            this->websocket->Disconnect();
            this->Reconnect(true);
            break;
        case 9: // invalid session
            this->log->info("Got invalid session message.");
            this->websocket->Disconnect();
            this->Reconnect(false);
            break;
        case 10: // open
            this->hb_int = msg["d"]["heartbeat_interval"].get<uint64_t>();
            this->log->debug("Got open message, hb_interval = {}", this->hb_int);
            this->hbThread = std::thread(&Socket::HBThreadMain, this);
            break;
        case 11: //hb ack
            this->haveAck = true;
            //this->log->info("Websocket heartbeat acknowledged.");
            if (!this->haveID) {
                this->SendIdentify();
                this->haveID = true;
            }
            this->gateway_latency = std::chrono::steady_clock::now() - this->last_hb_sent;
            break;
        default:
            this->log->warn("Unhandled opcode: {}", msg.at("op").get<int>());
            break;
        }
    }   
}

void Socket::SendHeartbeat(int64_t seq) {
    nlohmann::json msg = {
        { "op", 1 },
        { "d", (seq > 0 ? nlohmann::json(seq) : nlohmann::json(nullptr)) }
    };

    this->websocket->Send(msg.dump());
    this->last_hb_sent = std::chrono::steady_clock::now();
}

void Socket::SendIdentify() {
    nlohmann::json msg = {
        { "op", 2 },
        { "d", {
            { "token", this->token },
            { "properties", {
                { "os", "windows" },
                { "browser", "OctoPrintControl" },
                { "device", "OctoPrintControl" }
            }},
            { "intents", (1 << 9) | (1 << 15)}
        }}
    };

    this->websocket->Send(msg.dump());
}

void Socket::DispatchEvent(nlohmann::json event) {
    std::string eventName = event.at("t").get<std::string>();
    if (this->event_callbacks.count(eventName)) {
        for (SocketEventCallback &cb : this->event_callbacks[eventName]) {
            cb(eventName, event.at("d"));
        }
    }
}

void Socket::ProcessReadyEvent(std::string, nlohmann::json event) {
    this->session = event.at("session_id").get<std::string>();
    this->resume_url = event.at("resume_gateway_url").get<std::string>() + "?v10&encoding=json";
    this->log->info("Got session = {} and resume_url = {}", this->session, this->resume_url);
}

Interaction::Interaction(std::string token, std::string id)
:RESTClient(token), id(id) {
    this->log = spdlog::get("Interaction::"+id);
    if (!this->log.get()) this->log = spdlog::stdout_color_mt("Interaction::" + id);
}

void Interaction::CreateResponse(std::string token, int type) {
    nlohmann::json body = {
        { "type", type}
    };
    std::string endpoint = fmt::format("/interactions/{}/{}/callback", this->id, token);

    std::shared_ptr<HTTP::Request> req(new HTTP::Request);
    req->method = HTTP::RequestMethod::POST;
    req->body.reset(new HTTP::JSONRequestData(body));
    req->url = BASE_URL + endpoint;

    std::shared_ptr<HTTP::Response> resp = this->client->Perform(req);

    if (resp->code!=204) {
        this->log->error("Couldn't create interaction response");
    }
}

}