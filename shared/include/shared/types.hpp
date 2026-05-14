#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace shared {

enum class Mode {
    Auto,
    Manual
};

struct Profile {
    std::string name;
    std::string crop_type;
    double min_humidity_air {0.0};
    double max_humidity_air {0.0};
    double min_humidity_soil {0.0};
    double max_humidity_soil {0.0};
    int interval_sec {900};
    bool is_default {false};
};

struct ActiveConfig {
    Mode mode {Mode::Auto};
    std::string profile_name {"tomato"};
    int interval_sec {900};
    bool emergency_stop {false};
};

struct PlantBoxConfig {
    int id {0};
    std::string label;
    int slot {1};
    bool is_active {true};
};

struct TelemetrySample {
    std::string timestamp_utc;
    double temperature_c {0.0};
    double humidity_air {0.0};
    double humidity_soil {0.0};
    std::string pump_status {"off"};
    std::string motor_status {"closed"};
    std::string error_status {"none"};
};

inline std::string to_string(Mode mode) {
    switch (mode) {
        case Mode::Auto:
            return "auto";
        case Mode::Manual:
            return "manual";
    }
    return "auto";
}

inline std::optional<Mode> mode_from_string(std::string_view value) {
    if (value == "auto") {
        return Mode::Auto;
    }
    if (value == "manual") {
        return Mode::Manual;
    }
    return std::nullopt;
}

}  // namespace shared
