
#pragma once

#include <mutex>

#include "datahaandtering/database_manager.hpp"
#include "shared/types.hpp"

class ConfigManager {
public:
    explicit ConfigManager(DatabaseManager& db_manager);

    bool load_active_config();
    bool save_active_config();
    ActiveConfig current_config() const;
    void set_mode(Mode mode);
    void set_profile_name(const std::string& profile_name);
    void set_interval_sec(int interval_sec);
    void set_emergency_stop(bool emergency_stop);
    void set_ventilation_enabled(bool enabled);
    void set_watering_enabled(bool enabled);

private:
    DatabaseManager& db_manager_;
    mutable std::mutex config_mutex_;
    ActiveConfig active_config_ {};
};
