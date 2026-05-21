#include "sensor_manager.hpp"

#include "hw/Ventilation system/Motor.hpp"
#include "hw/Watering system/PlantBox.hpp"
#include "shared/logger.hpp"
#include "shared/time_utils.hpp"

namespace miljoestyring {

void SensorManager::set_hardware(Motor* motor, PlantBox* plant_box) {
    motor_ = motor;
    plant_box_ = plant_box;
}

TelemetrySample SensorManager::read_sample(const ClientRuntimeState* runtime) const {
    TelemetrySample sample;
    sample.timestamp_utc = now_utc_iso8601();

    if (motor_ == nullptr || plant_box_ == nullptr) {
        log_error("sensor_manager", "Hardware pointers not set");
        sample.error_status = "hardware_unavailable";
        return sample;
    }

    sample.temperature_c = static_cast<double>(motor_->getCurrentTemp());
    sample.humidity_air = static_cast<double>(motor_->getCurrentHumid());
    sample.humidity_soil = static_cast<double>(plant_box_->getCurrentAverageMoisture());
    // Kortvarige pump-events håndteres nu i app.cpp via immediate telemetry ved statusskift.
    sample.pump_status = plant_box_->isPumpOn() ? "on" : "off";
    sample.motor_status = motor_->isWindowOpen() ? "open" : "closed";
    if (runtime != nullptr && runtime->emergency_halt.load()) {
        sample.error_status = "emergency_stop";
    } else {
        sample.error_status = "none";
    }
    return sample;
}

}
