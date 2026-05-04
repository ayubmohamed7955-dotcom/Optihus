#include "miljoestyring/sensor_manager.hpp"

#include "shared/time_utils.hpp"

namespace miljoestyring {

shared::TelemetrySample SensorManager::read_sample() const {
    shared::TelemetrySample sample;
    sample.timestamp_utc = shared::now_utc_iso8601();
    sample.humidity_air = 60.0;
    sample.humidity_soil = 45.0;
    sample.pump_status = "off";
    sample.motor_status = "closed";
    sample.error_status = "none";
    return sample;
}

}  // namespace miljoestyring
