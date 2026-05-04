#include "miljoestyring/actuator_manager.hpp"

#include "shared/logger.hpp"

namespace miljoestyring {

void ActuatorManager::set_pump(bool on) {
    shared::log(shared::LogLevel::Info, "actuator_manager", on ? "Pump ON" : "Pump OFF");
}

void ActuatorManager::set_motor_state(const std::string& state) {
    shared::log(shared::LogLevel::Info, "actuator_manager", "Motor state set to " + state);
}

}  // namespace miljoestyring
