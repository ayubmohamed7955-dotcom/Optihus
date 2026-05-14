#include "shared/message_codec.hpp"

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "shared/time_utils.hpp"

namespace shared {

std::string message_type_to_string(MessageType type) {
    switch (type) {
        case MessageType::Hello:
            return "hello";
        case MessageType::Heartbeat:
            return "heartbeat";
        case MessageType::Telemetry:
            return "telemetry";
        case MessageType::Command:
            return "command";
        case MessageType::Ack:
            return "ack";
        case MessageType::Event:
            return "event";
        case MessageType::Error:
            return "error";
        case MessageType::Unknown:
            return "unknown";
    }
    return "unknown";
}

MessageType message_type_from_string(std::string_view type) {
    if (type == "hello") {
        return MessageType::Hello;
    }
    if (type == "heartbeat") {
        return MessageType::Heartbeat;
    }
    if (type == "telemetry") {
        return MessageType::Telemetry;
    }
    if (type == "command") {
        return MessageType::Command;
    }
    if (type == "ack") {
        return MessageType::Ack;
    }
    if (type == "event") {
        return MessageType::Event;
    }
    if (type == "error") {
        return MessageType::Error;
    }
    return MessageType::Unknown;
}

nlohmann::json to_json_obj(const HelloPayload& payload) {
    return nlohmann::json {
        {"node", payload.node},
        {"version", payload.version}
    };
}

nlohmann::json to_json_obj(const TelemetryPayload& payload) {
    return nlohmann::json {
        {"temperature_c", payload.temperature_c},
        {"humidity_air", payload.humidity_air},
        {"humidity_soil", payload.humidity_soil},
        {"pump_status", payload.pump_status},
        {"motor_status", payload.motor_status},
        {"error_status", payload.error_status}
    };
}

nlohmann::json to_json_obj(const CommandPayload& payload) {
    nlohmann::json j {
        {"action", payload.action}
    };
    if (!payload.params.is_null() && !payload.params.empty()) {
        j["params"] = payload.params;
    }
    return j;
}

nlohmann::json to_json_obj(const AckPayload& payload) {
    nlohmann::json j {
        {"ack_msg_id", payload.ack_msg_id},
        {"status", payload.status}
    };
    if (!payload.reason.empty()) {
        j["reason"] = payload.reason;
    }
    return j;
}

std::optional<HelloPayload> hello_from_json(const nlohmann::json& j) {
    HelloPayload payload;
    payload.node = j.value("node", "");
    payload.version = j.value("version", "");
    if (payload.node.empty()) {
        return std::nullopt;
    }
    return payload;
}

std::optional<TelemetryPayload> telemetry_from_json(const nlohmann::json& j) {
    if (!j.contains("humidity_air") || !j.contains("humidity_soil")) {
        return std::nullopt;
    }

    TelemetryPayload payload;
    payload.temperature_c = j.value("temperature_c", 0.0);
    payload.humidity_air = j.value("humidity_air", 0.0);
    payload.humidity_soil = j.value("humidity_soil", 0.0);
    payload.pump_status = j.value("pump_status", "off");
    payload.motor_status = j.value("motor_status", "closed");
    payload.error_status = j.value("error_status", "none");
    return payload;
}

std::optional<CommandPayload> command_from_json(const nlohmann::json& j) {
    CommandPayload payload;
    payload.action = j.value("action", "");
    payload.params = j.value("params", nlohmann::json::object());
    if (payload.action.empty()) {
        return std::nullopt;
    }
    return payload;
}

std::optional<AckPayload> ack_from_json(const nlohmann::json& j) {
    AckPayload payload;
    payload.ack_msg_id = j.value("ack_msg_id", "");
    payload.status = j.value("status", "ok");
    payload.reason = j.value("reason", "");
    if (payload.ack_msg_id.empty()) {
        return std::nullopt;
    }
    return payload;
}

std::string encode_json_line(const MessageEnvelope& message) {
    nlohmann::json j;
    j["type"] = message_type_to_string(message.type);
    j["ts"] = message.ts_utc;
    j["source"] = message.source;
    if (!message.msg_id.empty()) {
        j["msg_id"] = message.msg_id;
    }
    if (!message.data.is_null() && !message.data.empty()) {
        for (auto it = message.data.begin(); it != message.data.end(); ++it) {
            j[it.key()] = it.value();
        }
    }
    return j.dump() + "\n";
}

std::optional<MessageEnvelope> decode_json_line(std::string_view line) {
    try {
        const auto j = nlohmann::json::parse(std::string(line));
        MessageEnvelope message;
        message.type = message_type_from_string(j.value("type", ""));
        message.ts_utc = j.value("ts", "");
        message.source = j.value("source", "");
        message.msg_id = j.value("msg_id", "");
        message.data = j;
        message.data.erase("type");
        message.data.erase("ts");
        message.data.erase("source");
        message.data.erase("msg_id");

        if (message.type == MessageType::Unknown || message.source.empty() ||
            !is_iso8601_utc(message.ts_utc)) {
            return std::nullopt;
        }
        return message;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace shared
