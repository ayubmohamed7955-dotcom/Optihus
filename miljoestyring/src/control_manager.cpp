#include "control_manager.hpp"

#if OPTIHUS_MILJOESTYRING_HARDWARE
#include "hw/motor.hpp"
#include "hw/plant_box.hpp"
#endif
#include "shared/logger.hpp"
#include "shared/message_codec.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace miljoestyring {

namespace {

bool plant_box_entry_is_active(const nlohmann::json& box) {
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

PlantWateringDecision evaluate_plant_watering(const nlohmann::json& params) {
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

    double min_soil = box.value("min_soil_pct", 35.0);
    double max_soil = box.value("max_soil_pct", 100.0);
    int smin = static_cast<int>(std::lround(min_soil));
    int smax = static_cast<int>(std::lround(max_soil));
    smin = std::max(0, std::min(100, smin));
    smax = std::max(0, std::min(100, smax));
    if (smin > smax) {
        std::swap(smin, smax);
    }
    d.min_soil_pct = smin;
    d.max_soil_pct = smax;

    int pump = box.value("pump_duration_sec", 1);
    pump = std::max(1, std::min(3600, pump));
    d.pump_duration_sec = pump;

    int hyst = box.value("hysteresis_sec", 600);
    hyst = std::max(0, std::min(864000, hyst));
    d.hysteresis_sec = hyst;

    return d;
}

void log_plant_watering_decision(const PlantWateringDecision& decision) {
    switch (decision.mode) {
        case PlantWateringMode::MissingPlantBoxes:
            shared::log_warn(
                "control_manager",
                "Plant watering disabled (fail-safe): params.plant_boxes missing or not an array. "
                "Not using profile bounds for soil/pump."
            );
            break;
        case PlantWateringMode::NoActiveProfile:
            shared::log_info(
                "control_manager",
                "Plant watering disabled (fail-safe): zero active plant_boxes profiles. "
                "Not using profile bounds for soil/pump."
            );
            break;
        case PlantWateringMode::MultipleActiveProfiles:
            shared::log_warn(
                "control_manager",
                "Plant watering disabled (fail-safe): more than one active plant_boxes profile. "
                "Not using profile bounds for soil/pump."
            );
            break;
        case PlantWateringMode::SingleActiveProfile:
            shared::log_info(
                "control_manager",
                "Applied single active plant_boxes profile for soil/pump: id=" + std::to_string(decision.active_id) +
                    " label=\"" + decision.active_label + "\" min_soil_pct=" + std::to_string(decision.min_soil_pct) +
                    " max_soil_pct=" + std::to_string(decision.max_soil_pct) +
                    " pump_duration_sec=" + std::to_string(decision.pump_duration_sec) +
                    " hysteresis_sec=" + std::to_string(decision.hysteresis_sec) +
                    ". (Motor air limits still follow aggregated profile bounds when present.)"
            );
            break;
    }
}

#if OPTIHUS_MILJOESTYRING_HARDWARE
void apply_plant_water_decision(miljoestyring::hw::PlantBox* plant, const PlantWateringDecision& decision) {
    if (plant == nullptr) {
        return;
    }
    if (decision.mode == PlantWateringMode::SingleActiveProfile) {
        plant->setMinimumThreshold(decision.min_soil_pct);
        plant->setSkipWaterAtOrAbovePct(decision.max_soil_pct);
        plant->setPumpDurationSec(decision.pump_duration_sec);
        plant->setHysteresisSec(decision.hysteresis_sec);
    } else {
        // Internal fail-safe only (no watering): not an official control mode. Official rules are
        // "0 active plant_boxes => no watering" and ">1 active => no watering" (see README).
        plant->setMinimumThreshold(100);
        plant->setSkipWaterAtOrAbovePct(100);
        plant->setPumpDurationSec(1);
        plant->setHysteresisSec(600);
    }
}
#endif

}  // namespace

void ControlManager::apply_inbound_message(
    const shared::MessageEnvelope& message,
    ClientRuntimeState& runtime,
    miljoestyring::hw::Motor* motor,
    miljoestyring::hw::PlantBox* plant
) {
    if (message.type != shared::MessageType::Command) {
        return;
    }

    const auto cmd = shared::command_from_json(message.data);
    if (!cmd.has_value()) {
        shared::log_warn("control_manager", "Ignored command: could not parse payload");
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
        apply_plant_water_decision(plant, plant_water);
#endif

        if (em) {
#if OPTIHUS_MILJOESTYRING_HARDWARE
            if (motor != nullptr) {
                motor->stop();
            }
            if (plant != nullptr) {
                plant->stop();
            }
#endif
        } else {
#if OPTIHUS_MILJOESTYRING_HARDWARE
            if (motor != nullptr) {
                motor->start();
            }
            if (plant != nullptr) {
                plant->start();
            }
#endif
        }

        shared::log_info("control_manager", "Applied set_config from server");
        return;
    }

    if (cmd->action == "emergency_stop") {
        runtime.emergency_halt.store(true);
#if OPTIHUS_MILJOESTYRING_HARDWARE
        if (motor != nullptr) {
            motor->stop();
        }
        if (plant != nullptr) {
            plant->stop();
        }
#endif
        shared::log_warn("control_manager", "Emergency stop from server");
        return;
    }

    if (cmd->action == "clear_emergency_stop") {
        runtime.emergency_halt.store(false);
#if OPTIHUS_MILJOESTYRING_HARDWARE
        if (motor != nullptr) {
            motor->start();
        }
        if (plant != nullptr) {
            plant->start();
        }
#endif
        shared::log_info("control_manager", "Cleared emergency stop");
        return;
    }

    shared::log(shared::LogLevel::Info, "control_manager", "Unhandled command: " + cmd->action);
}

}  // namespace miljoestyring
