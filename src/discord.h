// OctoPrintControl - An OctoPrint Discord Bot
// Copyright (c) 2024 Taylor Talkington
// License: MIT (see LICENSE)
#pragma once
#include <thread>
#include <string>
#include <memory>
#include <functional>
#include <cinttypes>
#include <vector>
#include <map>
#include <chrono>
#include <list>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "http.h"
#include "websocket.h"

namespace OctoPrintControl::Discord {

class RESTClient {
public:
    RESTClient(std::string token);
    ~RESTClient();

private:
    std::string token;

    std::shared_ptr<spdlog::logger> log;

protected:
    std::shared_ptr<HTTP::Client> client;
};

struct ChannelMessageEmbedField {
    std::string name;
    std::string value;
    bool isinline = false;
};

struct ChannelMessageEmbed {
    std::string title;
    std::string description;
    int color = 16777215;
    std::list<std::shared_ptr<ChannelMessageEmbedField>> fields;
    std::string image_url;
};

struct ChannelMessageAttachment {
    std::string filename;
    std::string description;
    std::string contentType;
    std::vector<char> data;
};

class ChannelMessageComponent {
public:
    virtual nlohmann::json ToJSON() = 0;
};

class ActionRowComponent : public ChannelMessageComponent {
public:
    nlohmann::json ToJSON();
    void AddComponent(std::shared_ptr<ChannelMessageComponent> component);
private:
    std::list<std::shared_ptr<ChannelMessageComponent>> components;
};

class ButtonComponent : public ChannelMessageComponent {
public:
    ButtonComponent(int style, std::string label, std::string id) :style(style), label(label), id(id) {}
    nlohmann::json ToJSON();
private:
    int style;
    std::string label;
    std::string id;
};

typedef std::list<std::shared_ptr<ChannelMessageComponent>> ChannelMessageComponentList;
typedef std::vector<std::shared_ptr<ChannelMessageAttachment>> ChannelMessageAttachmentList;
typedef std::list<std::shared_ptr<ChannelMessageEmbed>> ChannelMessageEmbedList;

struct ChannelMessage {
    std::string id;
    std::string content;
    std::string reference_message;
    ChannelMessageAttachmentList attachments;
    ChannelMessageComponentList components;
    ChannelMessageEmbedList embeds;

    std::shared_ptr<HTTP::MultiPartRequestData> ToMultiPart();
};

inline std::shared_ptr<ChannelMessage> NewChannelMessage(std::string content="") {
    return std::shared_ptr<ChannelMessage>(new ChannelMessage{ .content=content });
}

inline std::shared_ptr<ChannelMessageEmbed> NewChannelMessageEmbed(std::string title="", std::string description="", int color=0xFFFFFF) {
    return std::shared_ptr<ChannelMessageEmbed>(
        new ChannelMessageEmbed{
            .title = title,
            .description = description,
            .color = color
        }
    );
}

inline std::shared_ptr<ChannelMessageEmbedField> NewChannelMessageEmbedField(std::string name, std::string value, bool isinline=false) {
    return std::shared_ptr<ChannelMessageEmbedField>(
        new ChannelMessageEmbedField{.name=name, .value=value, .isinline=isinline}
    );
}

class Channel: public RESTClient {
public:
    Channel(std::string token, std::string id);

    void CreateMessage(std::shared_ptr<ChannelMessage> message);
    void EditMessage(std::shared_ptr<ChannelMessage> message);
    void DeleteMessage(std::string id);
    void AddReaction(std::string message, std::string reaction);
    void TriggerTyping();

private:
    std::string id;
    std::shared_ptr<spdlog::logger> log;
};

typedef std::function<void(std::string, nlohmann::json)> SocketEventCallback;

class Socket {
public:
    Socket(std::string token);
    ~Socket();

    void AddEventCallback(std::string event, SocketEventCallback callback);
    std::chrono::duration<double, std::milli> GatewayLatency() { return this->gateway_latency; }

private:
    void HBThreadMain();

    void GetGatewayURL();

    void SendHeartbeat(int64_t seq);
    void SendIdentify();

    void OnWebsocketData(std::vector<char> data);

    void DispatchEvent(nlohmann::json event);

    void ProcessReadyEvent(std::string, nlohmann::json event);

    void Reconnect(bool resume=false);
    void ConnectThreadMain(bool resume);

    std::chrono::steady_clock::time_point last_hb_sent;
    std::chrono::duration<double, std::milli> gateway_latency;

    std::map<std::string, std::list<SocketEventCallback>> event_callbacks;

    std::string ws_url;

    std::shared_ptr<spdlog::logger> log;

    std::string token;

    std::string session;
    std::string resume_url;

    uint64_t hb_int = 0;
    int64_t seq = -1;
    bool haveAck = false;
    bool haveID = false;

    bool runHB = false;

    std::thread hbThread;

    std::shared_ptr<HTTP::Client> http;
    std::shared_ptr<Websocket::Client> websocket;
};

class Interaction : public RESTClient {
public:
    Interaction(std::string token, std::string id);

    void CreateResponse(std::string token, int type);

private:
    std::string id;
    std::shared_ptr<spdlog::logger> log;
};

}