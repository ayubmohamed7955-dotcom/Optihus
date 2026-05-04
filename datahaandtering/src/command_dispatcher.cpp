#include "datahaandtering/command_dispatcher.hpp"

#include <nlohmann/json.hpp>

#include "shared/constants.hpp"
#include "shared/time_utils.hpp"

namespace datahaandtering {

CommandDispatcher::CommandDispatcher(
    ConfigManager& config_manager,
    DatabaseManager& db_manager,
    shared::ThreadSafeQueue<shared::MessageEnvelope>& outbound_queue
)
    : config_manager_(config_manager), db_manager_(db_manager), outbound_queue_(outbound_queue) {}

void CommandDispatcher::dispatch_current_config() {
    const auto config = config_manager_.current_config();
    shared::CommandPayload payload;
    payload.action = "set_config";
    payload.params = nlohmann::json {
        {"mode", shared::to_string(config.mode)},
        {"profile_name", config.profile_name},
        {"interval_sec", config.interval_sec},
        {"emergency_stop", config.emergency_stop}
    };
    enqueue_command(payload, "queued");
}

void CommandDispatcher::dispatch_emergency_stop(bool enabled) {
    shared::CommandPayload payload;
    payload.action = enabled ? "emergency_stop" : "clear_emergency_stop";
    payload.params = nlohmann::json::object();
    enqueue_command(payload, "queued");
}

void CommandDispatcher::dispatch_manual_action(const std::string& action, const std::string& value) {
    shared::CommandPayload payload;
    payload.action = action;
    payload.params = nlohmann::json {
        {"value", value}
    };
    enqueue_command(payload, "queued");
}

void CommandDispatcher::enqueue_command(
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
}

}  // namespace datahaandtering
