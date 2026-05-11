#include "datahaandtering/command_dispatcher.hpp"

#include <sstream>
#include <vector>
#include <nlohmann/json.hpp>

#include "shared/constants.hpp"
#include "shared/time_utils.hpp"

namespace datahaandtering {

namespace {
std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> out;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto trim_left = item.find_first_not_of(" \t");
        const auto trim_right = item.find_last_not_of(" \t");
        if (trim_left == std::string::npos || trim_right == std::string::npos) {
            continue;
        }
        const auto trimmed = item.substr(trim_left, trim_right - trim_left + 1);
        if (!trimmed.empty()) {
            out.push_back(trimmed);
        }
    }
    return out;
}
}  // namespace

CommandDispatcher::CommandDispatcher(
    ConfigManager& config_manager,
    DatabaseManager& db_manager,
    shared::ThreadSafeQueue<shared::MessageEnvelope>& outbound_queue
)
    : config_manager_(config_manager), db_manager_(db_manager), outbound_queue_(outbound_queue) {}

bool CommandDispatcher::dispatch_current_config() {
    const auto config = config_manager_.current_config();
    const auto profile_names = split_csv(config.profile_name);
    shared::CommandPayload payload;
    payload.action = "set_config";
    payload.params = nlohmann::json {
        {"profile_name", config.profile_name},
        {"profile_names", profile_names},
        {"interval_sec", config.interval_sec},
        {"emergency_stop", config.emergency_stop}
    };
    return enqueue_command(payload, "queued");
}

bool CommandDispatcher::dispatch_emergency_stop(bool enabled) {
    shared::CommandPayload payload;
    payload.action = enabled ? "emergency_stop" : "clear_emergency_stop";
    payload.params = nlohmann::json::object();
    return enqueue_command(payload, "queued");
}

bool CommandDispatcher::dispatch_manual_action(const std::string& action, const std::string& value) {
    shared::CommandPayload payload;
    payload.action = action;
    payload.params = nlohmann::json {
        {"value", value}
    };
    return enqueue_command(payload, "queued");
}

bool CommandDispatcher::enqueue_command(
    const shared::CommandPayload& command_payload,
    const std::string& status
) {
    shared::MessageEnvelope message = shared::make_message(
        shared::MessageType::Command,
        std::string(shared::constants::kSourceDatahaandtering),
        shared::to_json_obj(command_payload),
        "",
        shared::now_utc_iso8601()
    );

    const bool queued = outbound_queue_.push(message);
    db_manager_.insert_command_history(
        message.ts_utc,
        command_payload.action,
        message.data.dump(),
        std::string(shared::constants::kSourceDatahaandtering),
        queued ? status : "dropped"
    );
    return queued;
}

}  // namespace datahaandtering
