#include "datahaandtering/tui_manager.hpp"

#include <iostream>
#include <limits>
#include <string>

#include "shared/logger.hpp"
#include "shared/time_utils.hpp"
#include "shared/types.hpp"

namespace datahaandtering {

TuiManager::TuiManager(DatabaseManager& db_manager, ConfigManager& config_manager)
    : db_manager_(db_manager), config_manager_(config_manager) {}

int TuiManager::run() {
    shared::log_info("tui", "Starting TUI skeleton");
    config_manager_.load_active_config();

    while (true) {
        print_menu();
        std::string choice;
        std::getline(std::cin, choice);

        if (choice == "1") {
            show_active_config();
        } else if (choice == "2") {
            list_profiles();
            select_profile();
        } else if (choice == "3") {
            create_profile();
        } else if (choice == "4") {
            set_mode();
        } else if (choice == "5") {
            toggle_emergency_stop();
        } else if (choice == "q" || choice == "Q") {
            break;
        } else {
            std::cout << "Unknown choice.\n";
        }
    }

    return 0;
}

void TuiManager::print_menu() const {
    std::cout << "\n=== Optihus TUI (skeleton) ===\n";
    std::cout << "1) Show active config\n";
    std::cout << "2) Select profile\n";
    std::cout << "3) Create custom profile\n";
    std::cout << "4) Set mode (auto/manual)\n";
    std::cout << "5) Toggle emergency stop\n";
    std::cout << "q) Quit\n";
    std::cout << "Choice: ";
}

void TuiManager::show_active_config() const {
    const auto cfg = config_manager_.current_config();
    std::cout << "Active profile: " << cfg.profile_name << "\n";
    std::cout << "Mode: " << shared::to_string(cfg.mode) << "\n";
    std::cout << "Interval sec: " << cfg.interval_sec << "\n";
    std::cout << "Emergency stop: " << (cfg.emergency_stop ? "ON" : "OFF") << "\n";
}

void TuiManager::list_profiles() const {
    const auto profiles = db_manager_.list_profiles();
    std::cout << "\nProfiles:\n";
    for (const auto& p : profiles) {
        std::cout << " - " << p.name << " (" << p.crop_type << "), air[" << p.min_humidity_air << ", "
                  << p.max_humidity_air << "], soil[" << p.min_humidity_soil << ", " << p.max_humidity_soil
                  << "], interval=" << p.interval_sec << "s\n";
    }
}

void TuiManager::select_profile() {
    std::cout << "Enter profile name: ";
    std::string name;
    std::getline(std::cin, name);
    if (name.empty()) {
        return;
    }
    config_manager_.set_profile_name(name);
    config_manager_.save_active_config();
}

void TuiManager::create_profile() {
    shared::Profile profile;
    profile.is_default = false;

    std::cout << "Name: ";
    std::getline(std::cin, profile.name);
    if (profile.name.empty()) {
        std::cout << "Cancelled.\n";
        return;
    }
    std::cout << "Crop type: ";
    std::getline(std::cin, profile.crop_type);

    std::cout << "Min air humidity: ";
    std::cin >> profile.min_humidity_air;
    std::cout << "Max air humidity: ";
    std::cin >> profile.max_humidity_air;
    std::cout << "Min soil humidity: ";
    std::cin >> profile.min_humidity_soil;
    std::cout << "Max soil humidity: ";
    std::cin >> profile.max_humidity_soil;
    std::cout << "Interval sec: ";
    std::cin >> profile.interval_sec;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (db_manager_.upsert_profile(profile, shared::now_utc_iso8601())) {
        std::cout << "Profile saved.\n";
    } else {
        std::cout << "Failed to save profile.\n";
    }
}

void TuiManager::set_mode() {
    std::cout << "Enter mode (auto/manual): ";
    std::string mode;
    std::getline(std::cin, mode);
    const auto parsed = shared::mode_from_string(mode);
    if (!parsed.has_value()) {
        std::cout << "Invalid mode.\n";
        return;
    }
    config_manager_.set_mode(*parsed);
    config_manager_.save_active_config();
}

void TuiManager::toggle_emergency_stop() {
    const auto cfg = config_manager_.current_config();
    config_manager_.set_emergency_stop(!cfg.emergency_stop);
    config_manager_.save_active_config();
}

}  // namespace datahaandtering
