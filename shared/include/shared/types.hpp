
#pragma once

#include <optional>
#include <string>
#include <string_view>

enum class Mode {
    Auto,
    Manual
};

struct Profile {

    std::string name;
    double min_humidity_air {0.0};
    double max_humidity_air {0.0};
    double min_humidity_soil {0.0};
    double max_humidity_soil {0.0};
    double min_temp_c {18.0};
    double max_temp_c {26.0};
    int interval_sec {60};
    bool is_default {false};
};

struct ActiveConfig {
    Mode mode {Mode::Auto};
    std::string profile_name {""};
    int interval_sec {60};
    bool emergency_stop {false};

    bool ventilation_enabled {false};

    bool watering_enabled {false};
};


inline constexpr double kPlantBoxUnsetMinSoilPct = -1.0;
inline constexpr int kPlantBoxUnsetHysteresisSec = -1;

inline constexpr const char* kDefaultPlantBoxLabel = "Plantekasse 1";
inline constexpr double kDefaultPlantMinSoilPct = 20.0;
inline constexpr int kDefaultPlantHysteresisSec = 30;


struct PlantBoxConfig {
    int id {0};
    std::string label;
    int slot {1};
    bool is_active {false};
    bool is_default {false};
    double min_soil_pct {kDefaultPlantMinSoilPct};
    int hysteresis_sec {kDefaultPlantHysteresisSec};
};

inline bool plant_box_min_soil_configured(const PlantBoxConfig& box) {
    return box.min_soil_pct >= 0.0 && box.min_soil_pct <= 100.0;
}

inline bool plant_box_hysteresis_configured(const PlantBoxConfig& box) {
    return box.hysteresis_sec >= 0;
}

inline bool plant_box_watering_params_configured(const PlantBoxConfig& box) {
    return plant_box_min_soil_configured(box) && plant_box_hysteresis_configured(box);
}

struct TelemetrySample {
    std::string timestamp_utc;
    double temperature_c {0.0};
    double humidity_air {0.0};
    double humidity_soil {0.0};
    std::string pump_status {"off"};
    std::string motor_status {"closed"};
    std::string error_status {"none"};

    std::string field_live_json;
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
