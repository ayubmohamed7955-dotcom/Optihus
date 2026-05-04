#pragma once

#include <string_view>

namespace shared::constants {

inline constexpr std::string_view kDataHostname = "datahaandtering-rpi";
inline constexpr std::string_view kMiljoeHostname = "miljoestyring-rpi";
inline constexpr std::string_view kHotspotSsid = "OptihusNet";
inline constexpr std::string_view kHotspotPassword = "Optihus123";
inline constexpr int kTcpPort = 5000;
inline constexpr std::string_view kDbPath = "/opt/optihus/data/optihus.db";
inline constexpr int kDefaultIntervalSec = 900;
inline constexpr int kHeartbeatIntervalSec = 10;
inline constexpr int kReconnectDelaySec = 3;
inline constexpr std::string_view kSourceDatahaandtering = "datahaandtering";
inline constexpr std::string_view kSourceMiljoestyring = "miljoestyring";

}  // namespace shared::constants
