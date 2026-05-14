#include "control_manager.hpp"

#if OPTIHUS_MILJOESTYRING_HARDWARE
#include "hw/motor.hpp"
#include "hw/plant_box.hpp"
#endif
#include "shared/logger.hpp"
#include "shared/message_codec.hpp"

#include <cmath>
#include <string>

namespace miljoestyring {

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

        if (p.contains("plant_boxes") && p["plant_boxes"].is_array()) {
            const auto box_count = p["plant_boxes"].size();
            shared::log_info(
                "control_manager",
                "Received plant box map (" + std::to_string(box_count)
                    + " boxes). Current hardware applies slot 1 only."
            );
        }

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
        if (plant != nullptr) {
            if (p.contains("bounds")) {
                const auto& b = p["bounds"];
                const int soil_min = static_cast<int>(std::lround(b.value("min_humidity_soil", 0.0)));
                const int soil_max = static_cast<int>(std::lround(b.value("max_humidity_soil", 100.0)));
                plant->setMinimumThreshold(soil_min);
                plant->setSkipWaterAtOrAbovePct(soil_max);
            } else {
                plant->setMinimumThreshold(35);
                plant->setSkipWaterAtOrAbovePct(100);
            }
        }

        if (em) {
            if (motor != nullptr) {
                motor->stop();
            }
            if (plant != nullptr) {
                plant->stop();
            }
        } else {
            if (motor != nullptr) {
                motor->start();
            }
            if (plant != nullptr) {
                plant->start();
            }
        }
#endif

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
