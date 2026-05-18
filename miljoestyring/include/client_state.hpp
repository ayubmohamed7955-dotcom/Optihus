#pragma once

#include <atomic>

#include "shared/constants.hpp"

struct ClientRuntimeState {
    std::atomic<int> telemetry_interval_sec {kDefaultIntervalSec};
    std::atomic<bool> emergency_halt {false};
    /** Sat ved første gyldige `set_config` fra server (web-UI-profil). Bruges så `clear_emergency_stop` ikke starter aktuatorer før konfiguration er set. */
    std::atomic<bool> server_config_received {false};
};
