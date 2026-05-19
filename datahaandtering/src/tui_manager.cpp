#include "datahaandtering/tui_manager.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "shared/logger.hpp"
#include "shared/time_utils.hpp"
#include "shared/types.hpp"

static std::vector<std::string> split_csv(const std::string& value) {
    std::vector<std::string> out;
    std::string item;
    for (char c : value) {
        if (c == ',') {
            if (!item.empty()) {
                out.push_back(item);
            }
            item.clear();
            continue;
        }
        item.push_back(c);
    }
    if (!item.empty()) {
        out.push_back(item);
    }

    for (auto& name : out) {
        const auto left = name.find_first_not_of(" \t");
        const auto right = name.find_last_not_of(" \t");
        if (left == std::string::npos || right == std::string::npos) {
            name.clear();
        } else {
            name = name.substr(left, right - left + 1);
        }
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const std::string& s) { return s.empty(); }), out.end());
    return out;
}

static std::vector<std::string> dedupe_preserve_order(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& item : items) {
        if (seen.insert(item).second) {
            out.push_back(item);
        }
    }
    return out;
}

static std::string join_csv(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += items[i];
        if (i + 1 < items.size()) {
            out += ",";
        }
    }
    return out;
}

TuiManager::TuiManager(DatabaseManager& db_manager, ConfigManager& config_manager)
    : db_manager_(db_manager), config_manager_(config_manager) {}

int TuiManager::run() {
    log_info("tui", "Starter TUI");
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
            list_profiles();
            delete_profile();
        } else if (choice == "5") {
            set_mode();
        } else if (choice == "6") {
            toggle_emergency_stop();
        } else if (choice == "q" || choice == "Q") {
            break;
        } else {
            std::cout << "Ukendt valg.\n";
        }
    }

    return 0;
}

void TuiManager::print_menu() const {
    std::cout << "\n=== Optihus TUI ===\n";
    std::cout << "1) Vis aktiv konfiguration\n";
    std::cout << "2) Vælg profiler\n";
    std::cout << "3) Opret eller opdater profil\n";
    std::cout << "4) Slet profil\n";
    std::cout << "5) Sæt tilstand (auto/manual)\n";
    std::cout << "6) Skift nødstop\n";
    std::cout << "q) Afslut\n";
    std::cout << "Valg: ";
}

void TuiManager::show_active_config() const {
    const auto cfg = config_manager_.current_config();
    const auto selected = dedupe_preserve_order(split_csv(cfg.profile_name));
    std::cout << "Valgte profiler: " << (selected.empty() ? "(ingen)" : join_csv(selected)) << "\n";
    std::cout << "Tilstand: " << to_string(cfg.mode) << "\n";
    std::cout << "Interval (sekunder): " << cfg.interval_sec << "\n";
    std::cout << "Nødstop: " << (cfg.emergency_stop ? "TIL" : "FRA") << "\n";
}

void TuiManager::list_profiles() const {
    const auto profiles = db_manager_.list_profiles();
    std::cout << "\nProfiler:\n";
    for (const auto& p : profiles) {
        std::cout << " - " << p.name << ", luftfugtighed ["
                  << p.min_humidity_air << ", " << p.max_humidity_air << "], jordfugtighed ["
                  << p.min_humidity_soil << ", " << p.max_humidity_soil << "], temp ["
                  << p.min_temp_c << ", " << p.max_temp_c
                  << "], interval=" << p.interval_sec << " sek\n";
    }
}

void TuiManager::select_profile() {
    const auto profiles = db_manager_.list_profiles();
    if (profiles.empty()) {
        std::cout << "Ingen profiler fundet. Opret mindst én profil først.\n";
        return;
    }
    std::set<std::string> available;
    for (const auto& p : profiles) {
        available.insert(p.name);
    }
    std::cout << "Indtast profilnavne adskilt med komma (mindst én): ";
    std::string input;
    std::getline(std::cin, input);
    auto selected = dedupe_preserve_order(split_csv(input));
    if (selected.empty()) {
        std::cout << "Du skal vælge mindst én profil.\n";
        return;
    }
    for (const auto& name : selected) {
        if (available.find(name) == available.end()) {
            std::cout << "Ukendt profil: " << name << "\n";
            return;
        }
    }
    config_manager_.set_profile_name(join_csv(selected));
    if (config_manager_.save_active_config()) {
        std::cout << "Aktiv konfiguration gemt.\n";
    } else {
        std::cout << "Kunne ikke gemme aktiv konfiguration.\n";
    }
}

void TuiManager::create_profile() {
    Profile profile;
    profile.is_default = false;

    std::cout << "Afgrøde-type (unikt profilnavn): ";
    std::getline(std::cin, profile.name);
    if (profile.name.empty()) {
        std::cout << "Annulleret.\n";
        return;
    }

    std::cout << "Minimum luftfugtighed: ";
    std::cin >> profile.min_humidity_air;
    std::cout << "Maksimum luftfugtighed: ";
    std::cin >> profile.max_humidity_air;
    std::cout << "Minimum jordfugtighed: ";
    std::cin >> profile.min_humidity_soil;
    std::cout << "Maksimum jordfugtighed: ";
    std::cin >> profile.max_humidity_soil;
    std::cout << "Minimum temperatur (C): ";
    std::cin >> profile.min_temp_c;
    std::cout << "Maksimum temperatur (C): ";
    std::cin >> profile.max_temp_c;
    std::cout << "Interval (sekunder): ";
    std::cin >> profile.interval_sec;
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if (db_manager_.upsert_profile(profile, now_utc_iso8601())) {
        std::cout << "Profil gemt.\n";
    } else {
        std::cout << "Kunne ikke gemme profil.\n";
    }
}

void TuiManager::delete_profile() {
    std::cout << "Indtast profilnavn der skal slettes: ";
    std::string name;
    std::getline(std::cin, name);
    if (name.empty()) {
        std::cout << "Annulleret.\n";
        return;
    }
    if (db_manager_.delete_profile(name)) {
        auto selected = dedupe_preserve_order(split_csv(config_manager_.current_config().profile_name));
        selected.erase(std::remove(selected.begin(), selected.end(), name), selected.end());
        if (!selected.empty()) {
            config_manager_.set_profile_name(join_csv(selected));
            config_manager_.save_active_config();
        } else {
            const auto remaining = db_manager_.list_profiles();
            if (!remaining.empty()) {
                config_manager_.set_profile_name(remaining.front().name);
                config_manager_.save_active_config();
            }
        }
        std::cout << "Profil slettet (hvis den fandtes).\n";
    } else {
        std::cout << "Kunne ikke slette profil.\n";
    }
}

void TuiManager::set_mode() {
    std::cout << "Indtast tilstand (auto/manual): ";
    std::string mode;
    std::getline(std::cin, mode);
    const auto parsed = mode_from_string(mode);
    if (!parsed.has_value()) {
        std::cout << "Ugyldig tilstand.\n";
        return;
    }
    config_manager_.set_mode(*parsed);
    if (config_manager_.save_active_config()) {
        std::cout << "Tilstand gemt.\n";
    } else {
        std::cout << "Kunne ikke gemme tilstand.\n";
    }
}

void TuiManager::toggle_emergency_stop() {
    const auto cfg = config_manager_.current_config();
    config_manager_.set_emergency_stop(!cfg.emergency_stop);
    if (config_manager_.save_active_config()) {
        std::cout << "Nødstop er nu " << (!cfg.emergency_stop ? "TIL" : "FRA") << ".\n";
    } else {
        std::cout << "Kunne ikke opdatere nødstop.\n";
    }
}

