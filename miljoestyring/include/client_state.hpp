#pragma once

#include <atomic>

#include "shared/constants.hpp"

namespace miljoestyring {

struct ClientRuntimeState {
    std::atomic<int> telemetry_interval_sec {shared::constants::kDefaultIntervalSec};
    std::atomic<bool> emergency_halt {false};
};

}  // namespace miljoestyring
