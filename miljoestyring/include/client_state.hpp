
#pragma once

#include <atomic>

#include "shared/constants.hpp"

struct ClientRuntimeState {
    std::atomic<int> telemetry_interval_sec {kDefaultIntervalSec};
    std::atomic<bool> emergency_halt {false};

    std::atomic<bool> server_config_received {false};
    std::atomic<bool> ventilation_enabled {true};
    std::atomic<bool> watering_enabled {true};

    std::atomic<bool> watering_actuation_allowed {false};
};
