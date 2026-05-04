#include "datahaandtering/config_manager.hpp"

#include "shared/constants.hpp"
#include "shared/time_utils.hpp"

namespace datahaandtering {

ConfigManager::ConfigManager(DatabaseManager& db_manager) : db_manager_(db_manager) {
    active_config_.profile_name = "tomato";
    active_config_.interval_sec = shared::constants::kDefaultIntervalSec;
}

bool ConfigManager::load_active_config() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return db_manager_.load_active_config(active_config_);
}

bool ConfigManager::save_active_config() {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return db_manager_.save_active_config(active_config_, shared::now_utc_iso8601());
}

shared::ActiveConfig ConfigManager::current_config() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return active_config_;
}

void ConfigManager::set_mode(shared::Mode mode) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    active_config_.mode = mode;
}

void ConfigManager::set_profile_name(const std::string& profile_name) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    active_config_.profile_name = profile_name;
}

void ConfigManager::set_interval_sec(int interval_sec) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    active_config_.interval_sec = interval_sec;
}

void ConfigManager::set_emergency_stop(bool emergency_stop) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    active_config_.emergency_stop = emergency_stop;
}

}  // namespace datahaandtering
