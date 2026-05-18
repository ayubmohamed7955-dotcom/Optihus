#include "datahaandtering/command_dispatcher.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/time_utils.hpp"

static std::vector<std::string> split_csv(const std::string& value) {
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

struct AggregatedBounds {
    double min_humidity_air {0.0};
    double max_humidity_air {0.0};
    double min_humidity_soil {0.0};
    double max_humidity_soil {0.0};
};

static bool aggregate_bounds_if_compatible(
    const std::vector<Profile>& selected_profiles,
    AggregatedBounds& out_bounds
) {
    if (selected_profiles.empty()) {
        return false;
    }
    out_bounds.min_humidity_air = selected_profiles.front().min_humidity_air;
    out_bounds.max_humidity_air = selected_profiles.front().max_humidity_air;
    out_bounds.min_humidity_soil = selected_profiles.front().min_humidity_soil;
    out_bounds.max_humidity_soil = selected_profiles.front().max_humidity_soil;

    for (std::size_t i = 1; i < selected_profiles.size(); ++i) {
        const auto& p = selected_profiles[i];
        out_bounds.min_humidity_air = std::max(out_bounds.min_humidity_air, p.min_humidity_air);
        out_bounds.max_humidity_air = std::min(out_bounds.max_humidity_air, p.max_humidity_air);
        out_bounds.min_humidity_soil = std::max(out_bounds.min_humidity_soil, p.min_humidity_soil);
        out_bounds.max_humidity_soil = std::min(out_bounds.max_humidity_soil, p.max_humidity_soil);
    }
    return out_bounds.min_humidity_air <= out_bounds.max_humidity_air &&
        out_bounds.min_humidity_soil <= out_bounds.max_humidity_soil;
}

static bool build_bounds_payload(
    DatabaseManager& db_manager,
    const std::vector<std::string>& profile_names,
    nlohmann::json& out_bounds,
    nlohmann::json& out_temp_bounds
) {
    const auto all = db_manager.list_profiles();
    std::vector<Profile> selected;
    selected.reserve(profile_names.size());
    for (const auto& name : profile_names) {
        const auto it =
            std::find_if(all.begin(), all.end(), [&](const Profile& p) { return p.name == name; });
        if (it != all.end()) {
            selected.push_back(*it);
        }
    }
    if (selected.empty()) {
        return false;
    }
    AggregatedBounds bounds {};
    if (!aggregate_bounds_if_compatible(selected, bounds)) {
        log_warn(
            "command_dispatcher",
            "Selected profiles have no overlapping humidity bounds; omitting bounds from set_config"
        );
        return false;
    }
    out_bounds = nlohmann::json {
        {"min_humidity_air", bounds.min_humidity_air},
        {"max_humidity_air", bounds.max_humidity_air},
        {"min_humidity_soil", bounds.min_humidity_soil},
        {"max_humidity_soil", bounds.max_humidity_soil},
    };
    // Profiles have no air temperature yet; send safe wide defaults for the window motor.
    out_temp_bounds = nlohmann::json {
        {"min", 12.0},
        {"max", 35.0},
    };
    return true;
}

static nlohmann::json build_plant_boxes_payload(DatabaseManager& db_manager) {
    const auto boxes = db_manager.list_plant_boxes();
    nlohmann::json out = nlohmann::json::array();
    for (const auto& box : boxes) {
        out.push_back(nlohmann::json {
            {"id", box.id},
            {"label", box.label},
            {"slot", box.slot},
            {"is_active", box.is_active},
            {"min_soil_pct", box.min_soil_pct},
            {"max_soil_pct", box.max_soil_pct},
            {"pump_duration_sec", box.pump_duration_sec},
            {"hysteresis_sec", box.hysteresis_sec},
        });
    }
    return out;
}

CommandDispatcher::CommandDispatcher(
    ConfigManager& config_manager,
    DatabaseManager& db_manager,
    ThreadSafeQueue<MessageEnvelope>& outbound_queue
)
    : config_manager_(config_manager), db_manager_(db_manager), outbound_queue_(outbound_queue) {}

bool CommandDispatcher::dispatch_current_config() {
    const auto config = config_manager_.current_config();
    const auto profile_names = split_csv(config.profile_name);
    nlohmann::json bounds;
    nlohmann::json temp_bounds;
    const bool have_bounds = build_bounds_payload(db_manager_, profile_names, bounds, temp_bounds);

    CommandPayload payload;
    payload.action = "set_config";
    payload.params = nlohmann::json {
        {"profile_name", config.profile_name},
        {"profile_names", profile_names},
        {"interval_sec", config.interval_sec},
        {"emergency_stop", config.emergency_stop},
        {"plant_boxes", build_plant_boxes_payload(db_manager_)},
    };
    if (have_bounds) {
        payload.params["bounds"] = std::move(bounds);
        payload.params["temp_bounds_c"] = std::move(temp_bounds);
    }
    return enqueue_command(payload, "queued");
}

bool CommandDispatcher::dispatch_emergency_stop(bool enabled) {
    CommandPayload payload;
    payload.action = enabled ? "emergency_stop" : "clear_emergency_stop";
    payload.params = nlohmann::json::object();
    return enqueue_command(payload, "queued");
}

bool CommandDispatcher::dispatch_manual_action(const std::string& action, const std::string& value) {
    CommandPayload payload;
    payload.action = action;
    payload.params = nlohmann::json {
        {"value", value}
    };
    return enqueue_command(payload, "queued");
}

bool CommandDispatcher::enqueue_command(
    const CommandPayload& command_payload,
    const std::string& status
) {
    MessageEnvelope message = make_message(
        MessageType::Command,
        std::string(kSourceDatahaandtering),
        to_json_obj(command_payload),
        "",
        now_utc_iso8601()
    );

    const bool queued = outbound_queue_.push(message);
    db_manager_.insert_command_history(
        message.ts_utc,
        command_payload.action,
        message.data.dump(),
        std::string(kSourceDatahaandtering),
        queued ? status : "dropped"
    );
    return queued;
}

