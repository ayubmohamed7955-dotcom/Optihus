#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace shared {

enum class MessageType {
    Hello,
    Heartbeat,
    Telemetry,
    Command,
    Ack,
    Event,
    Error,
    Unknown
};

struct MessageEnvelope {
    MessageType type {MessageType::Unknown};
    std::string ts_utc;
    std::string source;
    std::string msg_id;
    nlohmann::json data;
};

struct HelloPayload {
    std::string node;
    std::string version;
};

struct TelemetryPayload {
    double humidity_air {0.0};
    double humidity_soil {0.0};
    std::string pump_status {"off"};
    std::string motor_status {"closed"};
    std::string error_status {"none"};
};

struct CommandPayload {
    std::string action;
    nlohmann::json params;
};

struct AckPayload {
    std::string ack_msg_id;
    std::string status {"ok"};
    std::string reason;
};

std::string message_type_to_string(MessageType type);
MessageType message_type_from_string(std::string_view type);

nlohmann::json to_json_obj(const HelloPayload& payload);
nlohmann::json to_json_obj(const TelemetryPayload& payload);
nlohmann::json to_json_obj(const CommandPayload& payload);
nlohmann::json to_json_obj(const AckPayload& payload);

std::optional<HelloPayload> hello_from_json(const nlohmann::json& j);
std::optional<TelemetryPayload> telemetry_from_json(const nlohmann::json& j);
std::optional<CommandPayload> command_from_json(const nlohmann::json& j);
std::optional<AckPayload> ack_from_json(const nlohmann::json& j);

inline MessageEnvelope make_message(
    MessageType type,
    std::string source,
    nlohmann::json data = {},
    std::string msg_id = {},
    std::string ts_utc = {}
) {
    MessageEnvelope msg;
    msg.type = type;
    msg.source = std::move(source);
    msg.data = std::move(data);
    msg.msg_id = std::move(msg_id);
    msg.ts_utc = std::move(ts_utc);
    return msg;
}

}  // namespace shared
