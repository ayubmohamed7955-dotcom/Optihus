#include "sensor_manager.hpp"

#include "shared/time_utils.hpp"

#if OPTIHUS_MILJOESTYRING_HARDWARE
#include "hw/motor.hpp"
#include "hw/plant_box.hpp"
#endif

namespace miljoestyring {

void SensorManager::set_hardware(miljoestyring::hw::Motor* motor, miljoestyring::hw::PlantBox* plant) {
    motor_ = motor;
    plant_ = plant;
}

shared::TelemetrySample SensorManager::read_sample(const ClientRuntimeState* runtime) const {
    shared::TelemetrySample sample;
    sample.timestamp_utc = shared::now_utc_iso8601();

#if OPTIHUS_MILJOESTYRING_HARDWARE
    if (motor_ != nullptr && plant_ != nullptr) {
        sample.temperature_c = static_cast<double>(motor_->getCurrentTemp());
        sample.humidity_air = static_cast<double>(motor_->getCurrentHumid());
        sample.humidity_soil = static_cast<double>(plant_->getCurrentAverageMoisture());
        sample.pump_status = plant_->isPumpOn() ? "on" : "off";
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

}  // namespace miljoestyring
