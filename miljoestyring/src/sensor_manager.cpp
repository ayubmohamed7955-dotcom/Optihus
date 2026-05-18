#include "sensor_manager.hpp"

#include "shared/time_utils.hpp"

#if OPTIHUS_MILJOESTYRING_HARDWARE
#include "hw/Ventilation system/Motor.hpp"
#include "hw/Watering system/PlantBox.hpp"
#endif

namespace miljoestyring {

void SensorManager::set_hardware(Motor* motor, PlantBox* plant_box) {
    motor_ = motor;
    plant_box_ = plant_box;
}

TelemetrySample SensorManager::read_sample(const ClientRuntimeState* runtime) const {
    TelemetrySample sample;
    sample.timestamp_utc = now_utc_iso8601();

#if OPTIHUS_MILJOESTYRING_HARDWARE
    if (motor_ != nullptr && plant_box_ != nullptr) {
        sample.temperature_c = static_cast<double>(motor_->getCurrentTemp());
        sample.humidity_air = static_cast<double>(motor_->getCurrentHumid());
        sample.humidity_soil = static_cast<double>(plant_box_->getCurrentAverageMoisture());
        sample.pump_status = plant_box_->isPumpOn() ? "on" : "off";
        sample.motor_status = motor_->isWindowOpen() ? "open" : "closed";
        if (runtime != nullptr && runtime->emergency_halt.load()) {
            sample.error_status = "emergency_stop";
        } else {
            sample.error_status = "none";
        }
        return sample;
    }
#endif

    sample.temperature_c = 22.5;
    sample.humidity_air = 60.0;
    sample.humidity_soil = 45.0;
    sample.pump_status = "off";
    sample.motor_status = "closed";
    if (runtime != nullptr && runtime->emergency_halt.load()) {
        sample.error_status = "emergency_stop";
    } else {
        sample.error_status = "none";
    }
    return sample;
}

}
