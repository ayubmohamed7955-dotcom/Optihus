#include "miljoestyring/control_manager.hpp"

#include "shared/logger.hpp"
#include <string>

namespace miljoestyring {

void ControlManager::apply_config(const shared::ActiveConfig& config) {
    shared::log(shared::LogLevel::Info, "control_manager",
                "Applied config profile=" + config.profile_name);
}

void ControlManager::evaluate(const shared::TelemetrySample& sample) {
    shared::log(shared::LogLevel::Info, "control_manager",
                "Evaluate sample humidity_air=" + std::to_string(sample.humidity_air));
}

}  // namespace miljoestyring
