#include "control_manager.hpp"

#include "set_config_store.hpp"

#if OPTIHUS_MILJOESTYRING_HARDWARE
#include "hw/Ventilation system/Motor.hpp"
#include "hw/Watering system/PlantBox.hpp"
#endif
#include "shared/logger.hpp"
#include "shared/message_codec.hpp"
#include "shared/types.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

static bool plant_box_entry_is_active(const nlohmann::json& box) {
    if (!box.is_object() || !box.contains("is_active")) {
        return false;
    }
    const auto& v = box["is_active"];
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_number()) {
        return v.get<double>() != 0.0;
    }
    return false;
}

enum class PlantWateringMode {
    MissingPlantBoxes,
    NoActiveProfile,
    MultipleActiveProfiles,
    IncompleteActiveProfile,
    SingleActiveProfile,
};

struct PlantWateringDecision {
    PlantWateringMode mode {PlantWateringMode::MissingPlantBoxes};
    int min_soil_pct {100};
    int max_soil_pct {100};
    int pump_duration_sec {1};
    int hysteresis_sec {600};
    int active_id {0};
    std::string active_label;
};

static PlantWateringDecision evaluate_plant_watering(const nlohmann::json& params) {
    PlantWateringDecision d;
    if (!params.contains("plant_boxes") || !params["plant_boxes"].is_array()) {
        d.mode = PlantWateringMode::MissingPlantBoxes;
        return d;
    }
    const auto& arr = params["plant_boxes"];
    std::vector<std::size_t> active_indices;
    active_indices.reserve(arr.size());
    for (std::size_t i = 0; i < arr.size(); ++i) {
        if (!arr[i].is_object()) {
            continue;
        }
        if (plant_box_entry_is_active(arr[i])) {
            active_indices.push_back(i);
        }
    }
    if (active_indices.empty()) {
        d.mode = PlantWateringMode::NoActiveProfile;
        return d;
    }
    if (active_indices.size() > 1U) {
        d.mode = PlantWateringMode::MultipleActiveProfiles;
        return d;
    }

    const auto& box = arr[active_indices.front()];
    d.mode = PlantWateringMode::SingleActiveProfile;
    d.active_id = box.value("id", 0);
    if (box.contains("label") && box["label"].is_string()) {
        d.active_label = box["label"].get<std::string>();
    }

    double min_soil_raw = kPlantBoxUnsetMinSoilPct;
    if (box.contains("min_soil_pct") && !box["min_soil_pct"].is_null() && box["min_soil_pct"].is_number()) {
        min_soil_raw = box["min_soil_pct"].get<double>();
    }
    int hyst_raw = kPlantBoxUnsetHysteresisSec;
    if (box.contains("hysteresis_sec") && !box["hysteresis_sec"].is_null() && box["hysteresis_sec"].is_number()) {
        hyst_raw = box["hysteresis_sec"].get<int>();
    }
    PlantBoxConfig cfg;
    cfg.min_soil_pct = min_soil_raw;
    cfg.hysteresis_sec = hyst_raw;
    if (!plant_box_watering_params_configured(cfg)) {
        d.mode = PlantWateringMode::IncompleteActiveProfile;
        return d;
    }

    const int smin = static_cast<int>(std::lround(min_soil_raw));
    d.min_soil_pct = std::max(0, std::min(100, smin));
    d.max_soil_pct = 100;
    d.pump_duration_sec = 1;
    d.hysteresis_sec = std::max(0, std::min(86400, hyst_raw));

    return d;
}

static void log_plant_watering_decision(const PlantWateringDecision& decision) {
    switch (decision.mode) {
        case PlantWateringMode::MissingPlantBoxes:
            log_warn(
                "control_manager",
                "Plant watering disabled (fail-safe): params.plant_boxes missing or not an array. "
                "Not using profile bounds for soil/pump."
            );
            break;
        case PlantWateringMode::NoActiveProfile:
            log_info(
                "control_manager",
                "Plant watering disabled (fail-safe): zero active plant_boxes profiles. "
                "Not using profile bounds for soil/pump."
            );
            break;
        case PlantWateringMode::MultipleActiveProfiles:
            log_warn(
                "control_manager",
                "Plant watering disabled (fail-safe): more than one active plant_boxes profile. "
                "Not using profile bounds for soil/pump."
            );
            break;
        case PlantWateringMode::IncompleteActiveProfile:
            log_warn(
                "control_manager",
                "Plant watering disabled: active plant_boxes profile mangler min_soil_pct og/eller "
                "hysteresis_sec (skal udfyldes i web UI)."
            );
            break;
        case PlantWateringMode::SingleActiveProfile:
            log_info(
                "control_manager",
                "Applied single active plant_boxes profile: id=" + std::to_string(decision.active_id) +
                    " label=\"" + decision.active_label + "\" min_soil_pct=" + std::to_string(decision.min_soil_pct) +
                    " hysteresis_sec=" + std::to_string(decision.hysteresis_sec) +
                    " (setMinimumThreshold/setPauseBetweenWateringSec). Pump 1 s fixed in field (~25 ml). "
                    "Ventilation air limits still follow aggregated profile bounds."
            );
            break;
    }
}

#if OPTIHUS_MILJOESTYRING_HARDWARE
static void apply_plant_water_decision(PlantBox* plant_box_hw, const PlantWateringDecision& decision) {
    if (plant_box_hw == nullptr) {
        return;
    }
    if (decision.mode == PlantWateringMode::SingleActiveProfile) {
        plant_box_hw->setMinimumThreshold(decision.min_soil_pct);
        plant_box_hw->setPauseBetweenWateringSec(decision.hysteresis_sec);
    } else {
        plant_box_hw->setMinimumThreshold(100);
        plant_box_hw->setPauseBetweenWateringSec(86400);
    }
}
#endif


namespace miljoestyring {

void ControlManager::apply_inbound_message(
    const MessageEnvelope& message,
    ClientRuntimeState& runtime,
    Motor* motor,
    PlantBox* plant_box
) {
    if (message.type != MessageType::Command) {
        return;
    }

    const auto cmd = command_from_json(message.data);
    if (!cmd.has_value()) {
        log_warn("control_manager", "Ignored command: could not parse payload");
        return;
    }

    if (cmd->action == "set_config") {
        const auto& p = cmd->params;
        if (p.contains("interval_sec")) {
            const int sec = p.value("interval_sec", runtime.telemetry_interval_sec.load());
            if (sec >= 1 && sec <= 86400) {
                runtime.telemetry_interval_sec.store(sec);
            }
        }

        runtime.server_config_received.store(true);

        const bool em = p.value("emergency_stop", false);
        runtime.emergency_halt.store(em);

        const PlantWateringDecision plant_water = evaluate_plant_watering(p);
        log_plant_watering_decision(plant_water);

#if OPTIHUS_MILJOESTYRING_HARDWARE
        if (motor != nullptr) {
            if (p.contains("temp_bounds_c")) {
                const auto& tb = p["temp_bounds_c"];
                motor->setTemperatureLimits(
                    static_cast<float>(tb.value("min", 12.0)),
                    static_cast<float>(tb.value("max", 35.0))
                );
            } else {
                motor->setTemperatureLimits(12.0F, 35.0F);
            }
        }
        if (motor != nullptr) {
            if (p.contains("bounds")) {
                const auto& b = p["bounds"];
                motor->setHumidityLimits(
                    static_cast<float>(b.value("min_humidity_air", 0.0)),
                    static_cast<float>(b.value("max_humidity_air", 100.0))
                );
            } else {
                motor->setHumidityLimits(40.0F, 75.0F);
            }
        }
        apply_plant_water_decision(plant_box, plant_water);
#endif

#if OPTIHUS_MILJOESTYRING_HARDWARE
        const bool allow_motion = !em;
        if (motor != nullptr) {
            motor->setActuationEnabled(allow_motion);
        }
        if (plant_box != nullptr) {
            plant_box->setActuationEnabled(allow_motion);
        }
#endif

        (void)persist_field_config_after_set_config(p, runtime.emergency_halt.load());

        log_info("control_manager", "Applied set_config from server");
        return;
    }

    if (cmd->action == "emergency_stop") {
        runtime.emergency_halt.store(true);
#if OPTIHUS_MILJOESTYRING_HARDWARE
        if (motor != nullptr) {
            motor->setActuationEnabled(false);
        }
        if (plant_box != nullptr) {
            plant_box->setActuationEnabled(false);
        }
#endif
        (void)persist_field_config_emergency_overlay(true);
        log_warn("control_manager", "Emergency stop from server");
        return;
    }

    if (cmd->action == "clear_emergency_stop") {
        runtime.emergency_halt.store(false);
#if OPTIHUS_MILJOESTYRING_HARDWARE
        if (runtime.server_config_received.load() && motor != nullptr && plant_box != nullptr) {
            motor->setActuationEnabled(true);
            plant_box->setActuationEnabled(true);
        }
#endif
        (void)persist_field_config_emergency_overlay(false);
        log_info("control_manager", "Cleared emergency stop");
        return;
    }

    log(LogLevel::Info, "control_manager", "Unhandled command: " + cmd->action);
}

}

