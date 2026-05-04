#pragma once

#include "datahaandtering/config_manager.hpp"
#include "datahaandtering/database_manager.hpp"

namespace datahaandtering {

class TuiManager {
public:
    TuiManager(DatabaseManager& db_manager, ConfigManager& config_manager);
    int run();

private:
    void print_menu() const;
    void show_active_config() const;
    void list_profiles() const;
    void select_profile();
    void create_profile();
    void set_mode();
    void toggle_emergency_stop();

    DatabaseManager& db_manager_;
    ConfigManager& config_manager_;
};

}  // namespace datahaandtering
