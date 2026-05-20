#include "datahaandtering/database_manager.hpp"

#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "shared/logger.hpp"
#include "shared/time_utils.hpp"
#include "shared/types.hpp"

#include <array>

static int bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

static std::string col_text(sqlite3_stmt* stmt, int index) {
    const unsigned char* raw = sqlite3_column_text(stmt, index);
    return raw == nullptr ? "" : reinterpret_cast<const char*>(raw);
}

static bool telemetry_has_temperature_column(sqlite3* db) {
    const char* sql = "PRAGMA table_info(telemetry);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto name = col_text(stmt, 1);
        if (name == "temperature_c") {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static bool sqlite_table_exists(sqlite3* db, const char* table_name) {
    const char* sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    const bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static bool telemetry_table_exists(sqlite3* db) {
    return sqlite_table_exists(db, "telemetry");
}

static bool profiles_has_column(sqlite3* db, const char* column_name) {
    const char* sql = "PRAGMA table_info(profiles);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (col_text(stmt, 1) == column_name) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static bool active_config_has_column(sqlite3* db, const char* column_name) {
    const char* sql = "PRAGMA table_info(active_config);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (col_text(stmt, 1) == column_name) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static bool plant_boxes_has_column(sqlite3* db, const char* column_name) {
    const char* sql = "PRAGMA table_info(plant_boxes);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (col_text(stmt, 1) == column_name) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

DatabaseManager::DatabaseManager(std::string db_path) : db_path_(std::move(db_path)) {}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ != nullptr) {
        return true;
    }

    const int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK || db_ == nullptr) {
        log_error("database_manager", "sqlite3_open failed for " + db_path_);
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    if (telemetry_table_exists(db_) && !telemetry_has_temperature_column(db_)) {
        char* err = nullptr;
        const int alter_rc =
            sqlite3_exec(db_, "ALTER TABLE telemetry ADD COLUMN temperature_c REAL;", nullptr, nullptr, &err);
        if (alter_rc != SQLITE_OK) {
            const std::string msg = err == nullptr ? "unknown" : err;
            if (err != nullptr) {
                sqlite3_free(err);
            }
            log_error("database_manager", "Failed to add telemetry.temperature_c column: " + msg);
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }
        log_info("database_manager", "Applied DB migration: telemetry.temperature_c");
    }
    if (!ensure_plant_boxes_schema_locked()) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }
    log_info("database_manager", "Opened SQLite: " + db_path_);
    return true;
}

bool DatabaseManager::apply_schema_migrations() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    return apply_schema_migrations_locked();
}

bool DatabaseManager::apply_schema_migrations_locked() {
    if (db_ == nullptr) {
        return false;
    }

    if (sqlite_table_exists(db_, "profiles")) {
        const struct ProfileTempMigration {
            const char* name;
            const char* sql;
        } profile_migrations[] = {
            {"min_temp_c", "ALTER TABLE profiles ADD COLUMN min_temp_c REAL NOT NULL DEFAULT 18;"},
            {"max_temp_c", "ALTER TABLE profiles ADD COLUMN max_temp_c REAL NOT NULL DEFAULT 26;"},
        };
        for (const auto& m : profile_migrations) {
            if (profiles_has_column(db_, m.name)) {
                continue;
            }
            char* err = nullptr;
            if (sqlite3_exec(db_, m.sql, nullptr, nullptr, &err) != SQLITE_OK) {
                const std::string msg = err == nullptr ? "unknown" : err;
                if (err != nullptr) {
                    sqlite3_free(err);
                }
                log_error(
                    "database_manager",
                    std::string("profiles migration failed for ") + m.name + ": " + msg
                );
                return false;
            }
            log_info("database_manager", std::string("Applied profiles migration: ") + m.name);
        }
        if (profiles_has_column(db_, "min_temp_c") && profiles_has_column(db_, "max_temp_c")) {
            char* repair_err = nullptr;
            const char* repair_sql =
                "UPDATE profiles SET min_temp_c = 18.0, max_temp_c = 26.0 "
                "WHERE (min_temp_c = 0.0 AND max_temp_c = 0.0) OR min_temp_c > max_temp_c;";
            if (sqlite3_exec(db_, repair_sql, nullptr, nullptr, &repair_err) != SQLITE_OK) {
                const std::string msg = repair_err == nullptr ? "unknown" : repair_err;
                if (repair_err != nullptr) {
                    sqlite3_free(repair_err);
                }
                log_warn("database_manager", "profiles temperature repair skipped: " + msg);
            } else {
                log_info("database_manager", "Repaired invalid profile temperature bounds where needed");
            }
        }
    }

    if (sqlite_table_exists(db_, "active_config")) {
        const struct ActiveConfigMigration {
            const char* name;
            const char* sql;
        } active_config_migrations[] = {
            {"ventilation_enabled",
             "ALTER TABLE active_config ADD COLUMN ventilation_enabled INTEGER NOT NULL DEFAULT 1 "
             "CHECK (ventilation_enabled IN (0, 1));"},
            {"watering_enabled",
             "ALTER TABLE active_config ADD COLUMN watering_enabled INTEGER NOT NULL DEFAULT 1 "
             "CHECK (watering_enabled IN (0, 1));"},
        };
        for (const auto& m : active_config_migrations) {
            if (active_config_has_column(db_, m.name)) {
                continue;
            }
            char* err = nullptr;
            if (sqlite3_exec(db_, m.sql, nullptr, nullptr, &err) != SQLITE_OK) {
                const std::string msg = err == nullptr ? "unknown" : err;
                if (err != nullptr) {
                    sqlite3_free(err);
                }
                log_error(
                    "database_manager",
                    std::string("active_config migration failed for ") + m.name + ": " + msg
                );
                return false;
            }
            log_info("database_manager", std::string("Applied active_config migration: ") + m.name);
        }
    }

    return true;
}

void DatabaseManager::close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return;
    }
    sqlite3_close(db_);
    db_ = nullptr;
    log_info("database_manager", "Closed SQLite");
}

bool DatabaseManager::initialize_schema(const std::string& init_sql_path) {
    std::ifstream file(init_sql_path);
    if (!file.is_open()) {
        log_error("database_manager", "Could not open schema file: " + init_sql_path);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return exec_sql(buffer.str());
}

bool DatabaseManager::insert_telemetry(
    const std::string& timestamp_utc,
    const TelemetrySample& sample,
    const std::string& source
) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO telemetry(timestamp, temperature_c, humidity_air, humidity_soil, pump_status, motor_status, "
        "error_status, source) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, timestamp_utc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, sample.temperature_c);
    sqlite3_bind_double(stmt, 3, sample.humidity_air);
    sqlite3_bind_double(stmt, 4, sample.humidity_soil);
    sqlite3_bind_text(stmt, 5, sample.pump_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, sample.motor_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, sample.error_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, source.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int DatabaseManager::prune_telemetry_older_than_days(int days) {
    if (days < 1) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return 0;
    }

    const std::string cutoff = utc_iso8601_days_ago(days);
    const char* sql = "DELETE FROM telemetry WHERE timestamp < ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        log_error("database_manager", "Could not prepare telemetry prune statement");
        return 0;
    }
    bind_text(stmt, 1, cutoff);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) {
        log_error("database_manager", "Telemetry prune failed");
        return 0;
    }

    const int removed = sqlite3_changes(db_);
    if (removed > 0) {
        log_info(
            "database_manager",
            "Pruned telemetry older than " + std::to_string(days)
                + " days (cutoff " + cutoff + "): removed " + std::to_string(removed) + " rows"
        );
    }
    return removed;
}

bool DatabaseManager::insert_system_event(
    const std::string& timestamp_utc,
    const std::string& event_type,
    const std::string& description,
    const std::string& severity
) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO system_events(timestamp, event_type, description, severity) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bind_text(stmt, 1, timestamp_utc);
    bind_text(stmt, 2, event_type);
    bind_text(stmt, 3, description);
    bind_text(stmt, 4, severity);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::insert_command_history(
    const std::string& timestamp_utc,
    const std::string& action,
    const std::string& payload_json,
    const std::string& source,
    const std::string& status
) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO command_history(timestamp, action, payload_json, source, status) VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bind_text(stmt, 1, timestamp_utc);
    bind_text(stmt, 2, action);
    bind_text(stmt, 3, payload_json);
    bind_text(stmt, 4, source);
    bind_text(stmt, 5, status);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::upsert_profile(const Profile& profile, const std::string& timestamp_utc) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO profiles(name, crop_type, min_humidity_air, max_humidity_air, min_humidity_soil, "
        "max_humidity_soil, min_temp_c, max_temp_c, interval_sec, is_default, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(name) DO UPDATE SET "
        "crop_type=excluded.crop_type, min_humidity_air=excluded.min_humidity_air, "
        "max_humidity_air=excluded.max_humidity_air, min_humidity_soil=excluded.min_humidity_soil, "
        "max_humidity_soil=excluded.max_humidity_soil, min_temp_c=excluded.min_temp_c, "
        "max_temp_c=excluded.max_temp_c, interval_sec=excluded.interval_sec, "
        "is_default=excluded.is_default, updated_at=excluded.updated_at;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bind_text(stmt, 1, profile.name);
    bind_text(stmt, 2, profile.name);
    sqlite3_bind_double(stmt, 3, profile.min_humidity_air);
    sqlite3_bind_double(stmt, 4, profile.max_humidity_air);
    sqlite3_bind_double(stmt, 5, profile.min_humidity_soil);
    sqlite3_bind_double(stmt, 6, profile.max_humidity_soil);
    sqlite3_bind_double(stmt, 7, profile.min_temp_c);
    sqlite3_bind_double(stmt, 8, profile.max_temp_c);
    sqlite3_bind_int(stmt, 9, profile.interval_sec);
    sqlite3_bind_int(stmt, 10, profile.is_default ? 1 : 0);
    bind_text(stmt, 11, timestamp_utc);
    bind_text(stmt, 12, timestamp_utc);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::delete_profile(const std::string& profile_name) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr || profile_name.empty()) {
        return false;
    }

    const char* sql = "DELETE FROM profiles WHERE name = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bind_text(stmt, 1, profile_name);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Profile> DatabaseManager::list_profiles() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<Profile> result;
    if (db_ == nullptr) {
        return result;
    }

    const char* sql =
        "SELECT name, crop_type, min_humidity_air, max_humidity_air, min_humidity_soil, "
        "max_humidity_soil, min_temp_c, max_temp_c, interval_sec, is_default FROM profiles ORDER BY name;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Profile p;
        p.name = col_text(stmt, 0);
        p.min_humidity_air = sqlite3_column_double(stmt, 2);
        p.max_humidity_air = sqlite3_column_double(stmt, 3);
        p.min_humidity_soil = sqlite3_column_double(stmt, 4);
        p.max_humidity_soil = sqlite3_column_double(stmt, 5);
        p.min_temp_c = sqlite3_column_double(stmt, 6);
        p.max_temp_c = sqlite3_column_double(stmt, 7);
        p.interval_sec = sqlite3_column_int(stmt, 8);
        p.is_default = sqlite3_column_int(stmt, 9) == 1;
        result.push_back(std::move(p));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool DatabaseManager::upsert_plant_box(
    const PlantBoxConfig& plant_box,
    const std::string& timestamp_utc
) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr || plant_box.label.empty() || plant_box.slot < 1) {
        return false;
    }

    const bool has_id = plant_box.id > 0;
    if (plant_box.is_active) {
        const char* count_sql = has_id ? "SELECT COUNT(*) FROM plant_boxes WHERE is_active = 1 AND id != ?;"
                                       : "SELECT COUNT(*) FROM plant_boxes WHERE is_active = 1;";
        sqlite3_stmt* count_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        if (has_id) {
            sqlite3_bind_int(count_stmt, 1, plant_box.id);
        }
        if (sqlite3_step(count_stmt) != SQLITE_ROW) {
            sqlite3_finalize(count_stmt);
            return false;
        }
        const int other_active = sqlite3_column_int(count_stmt, 0);
        sqlite3_finalize(count_stmt);
        if (other_active > 0) {
            log_warn("database_manager",
                              "Rejected plant_boxes upsert: another row is already active (only one active allowed)");
            return false;
        }
    }

    const char* sql_insert =
        "INSERT INTO plant_boxes(label, slot, is_active, is_default, min_soil_pct, max_soil_pct, pump_duration_sec, "
        "hysteresis_sec, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    const char* sql_update =
        "UPDATE plant_boxes SET label = ?, slot = ?, is_active = ?, is_default = ?, min_soil_pct = ?, max_soil_pct = ?, "
        "pump_duration_sec = ?, hysteresis_sec = ?, updated_at = ? WHERE id = ?;";

    sqlite3_stmt* stmt = nullptr;
    const char* sql = has_id ? sql_update : sql_insert;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bind_text(stmt, 1, plant_box.label);
    sqlite3_bind_int(stmt, 2, plant_box.slot);
    sqlite3_bind_int(stmt, 3, plant_box.is_active ? 1 : 0);
    sqlite3_bind_int(stmt, 4, plant_box.is_default ? 1 : 0);
    sqlite3_bind_double(stmt, 5, plant_box.min_soil_pct);
    sqlite3_bind_double(stmt, 6, plant_box.max_soil_pct);
    sqlite3_bind_int(stmt, 7, plant_box.pump_duration_sec);
    sqlite3_bind_int(stmt, 8, plant_box.hysteresis_sec);
    if (has_id) {
        bind_text(stmt, 9, timestamp_utc);
        sqlite3_bind_int(stmt, 10, plant_box.id);
    } else {
        bind_text(stmt, 9, timestamp_utc);
        bind_text(stmt, 10, timestamp_utc);
    }

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::set_plant_box_active(int id, bool active, const std::string& timestamp_utc) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr || id <= 0) {
        return false;
    }

    if (active) {
        const char* count_sql = "SELECT COUNT(*) FROM plant_boxes WHERE is_active = 1 AND id != ?;";
        sqlite3_stmt* count_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_int(count_stmt, 1, id);
        if (sqlite3_step(count_stmt) != SQLITE_ROW) {
            sqlite3_finalize(count_stmt);
            return false;
        }
        const int other_active = sqlite3_column_int(count_stmt, 0);
        sqlite3_finalize(count_stmt);
        if (other_active > 0) {
            log_warn("database_manager",
                              "Rejected plant_boxes activate: another row is already active");
            return false;
        }
    }

    const char* sql = "UPDATE plant_boxes SET is_active = ?, updated_at = ? WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, active ? 1 : 0);
    bind_text(stmt, 2, timestamp_utc);
    sqlite3_bind_int(stmt, 3, id);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::delete_plant_box(int id) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr || id <= 0) {
        return false;
    }

    const char* default_sql = "SELECT is_default FROM plant_boxes WHERE id = ?;";
    sqlite3_stmt* default_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, default_sql, -1, &default_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(default_stmt, 1, id);
    if (sqlite3_step(default_stmt) != SQLITE_ROW) {
        sqlite3_finalize(default_stmt);
        return false;
    }
    const bool is_default = sqlite3_column_int(default_stmt, 0) == 1;
    sqlite3_finalize(default_stmt);
    if (is_default) {
        log_warn("database_manager", "Rejected plant_boxes delete: default profile cannot be deleted");
        return false;
    }

    const char* count_sql = "SELECT COUNT(*) FROM plant_boxes;";
    sqlite3_stmt* count_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    int total = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);
    if (total <= 1) {
        return false;
    }

    const char* sql = "DELETE FROM plant_boxes WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<PlantBoxConfig> DatabaseManager::list_plant_boxes() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<PlantBoxConfig> result;
    if (db_ == nullptr) {
        return result;
    }

    const char* sql =
        "SELECT id, label, slot, is_active, is_default, min_soil_pct, max_soil_pct, pump_duration_sec, "
        "hysteresis_sec FROM plant_boxes ORDER BY is_default DESC, slot, id;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PlantBoxConfig plant_box;
        plant_box.id = sqlite3_column_int(stmt, 0);
        plant_box.label = col_text(stmt, 1);
        plant_box.slot = sqlite3_column_int(stmt, 2);
        plant_box.is_active = sqlite3_column_int(stmt, 3) == 1;
        plant_box.is_default = sqlite3_column_int(stmt, 4) == 1;
        plant_box.min_soil_pct = sqlite3_column_double(stmt, 5);
        plant_box.max_soil_pct = sqlite3_column_double(stmt, 6);
        plant_box.pump_duration_sec = sqlite3_column_int(stmt, 7);
        plant_box.hysteresis_sec = sqlite3_column_int(stmt, 8);
        result.push_back(std::move(plant_box));
    }
    sqlite3_finalize(stmt);
    return result;
}

bool DatabaseManager::load_active_config(ActiveConfig& out_config) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "SELECT mode, profile_name, interval_sec, emergency_stop, "
        "COALESCE(ventilation_enabled, 1), COALESCE(watering_enabled, 1) "
        "FROM active_config WHERE id=1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto mode_str = col_text(stmt, 0);
        const auto parsed = mode_from_string(mode_str);
        if (parsed.has_value()) {
            out_config.mode = *parsed;
            out_config.profile_name = col_text(stmt, 1);
            out_config.interval_sec = sqlite3_column_int(stmt, 2);
            out_config.emergency_stop = sqlite3_column_int(stmt, 3) == 1;
            out_config.ventilation_enabled = sqlite3_column_int(stmt, 4) == 1;
            out_config.watering_enabled = sqlite3_column_int(stmt, 5) == 1;
            ok = true;
        }
    }

    sqlite3_finalize(stmt);
    return ok;
}

std::string DatabaseManager::read_active_config_timestamp_utc() const {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return {};
    }
    const char* sql = "SELECT timestamp FROM active_config WHERE id=1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    std::string out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        out = col_text(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return out;
}

bool DatabaseManager::save_active_config(
    const ActiveConfig& config,
    const std::string& timestamp_utc
) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO active_config(id, timestamp, mode, profile_name, interval_sec, emergency_stop, "
        "ventilation_enabled, watering_enabled) "
        "VALUES(1, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET timestamp=excluded.timestamp, mode=excluded.mode, "
        "profile_name=excluded.profile_name, interval_sec=excluded.interval_sec, "
        "emergency_stop=excluded.emergency_stop, "
        "ventilation_enabled=excluded.ventilation_enabled, "
        "watering_enabled=excluded.watering_enabled;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bind_text(stmt, 1, timestamp_utc);
    bind_text(stmt, 2, to_string(config.mode));
    bind_text(stmt, 3, config.profile_name);
    sqlite3_bind_int(stmt, 4, config.interval_sec);
    sqlite3_bind_int(stmt, 5, config.emergency_stop ? 1 : 0);
    sqlite3_bind_int(stmt, 6, config.ventilation_enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 7, config.watering_enabled ? 1 : 0);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

const std::string& DatabaseManager::db_path() const {
    return db_path_;
}

bool DatabaseManager::exec_sql(const std::string& sql) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }
    char* error_message = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        const std::string error = (error_message == nullptr) ? "unknown" : error_message;
        if (error_message != nullptr) {
            sqlite3_free(error_message);
        }
        log_error("database_manager", "SQL execution failed: " + error);
        return false;
    }
    return true;
}

bool DatabaseManager::ensure_plant_boxes_schema_locked() {
    // Must be called with db_mutex_ held (see open()).
    if (db_ == nullptr) {
        return false;
    }

    const char* schema_sql =
        "CREATE TABLE IF NOT EXISTS plant_boxes ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "label TEXT NOT NULL,"
        "slot INTEGER NOT NULL UNIQUE,"
        "is_active INTEGER NOT NULL DEFAULT 0 CHECK (is_active IN (0, 1)),"
        "is_default INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1)),"
        "min_soil_pct REAL NOT NULL DEFAULT 20,"
        "max_soil_pct REAL NOT NULL DEFAULT 100,"
        "pump_duration_sec INTEGER NOT NULL DEFAULT 1,"
        "hysteresis_sec INTEGER NOT NULL DEFAULT 30,"
        "created_at TEXT NOT NULL,"
        "updated_at TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_plant_boxes_slot ON plant_boxes(slot);";

    char* error_message = nullptr;
    const int rc = sqlite3_exec(db_, schema_sql, nullptr, nullptr, &error_message);
    if (rc != SQLITE_OK) {
        const std::string error = (error_message == nullptr) ? "unknown" : error_message;
        if (error_message != nullptr) {
            sqlite3_free(error_message);
        }
        log_error("database_manager", "Plant boxes schema setup failed: " + error);
        return false;
    }

    static const struct {
        const char* name;
        const char* ddl;
    } k_migrations[] = {
        {"min_soil_pct", "ALTER TABLE plant_boxes ADD COLUMN min_soil_pct REAL NOT NULL DEFAULT 35;"},
        {"max_soil_pct", "ALTER TABLE plant_boxes ADD COLUMN max_soil_pct REAL NOT NULL DEFAULT 100;"},
        {"pump_duration_sec", "ALTER TABLE plant_boxes ADD COLUMN pump_duration_sec INTEGER NOT NULL DEFAULT 1;"},
        {"hysteresis_sec", "ALTER TABLE plant_boxes ADD COLUMN hysteresis_sec INTEGER NOT NULL DEFAULT 600;"},
        {"is_default", "ALTER TABLE plant_boxes ADD COLUMN is_default INTEGER NOT NULL DEFAULT 0;"},
    };

    for (const auto& m : k_migrations) {
        if (plant_boxes_has_column(db_, m.name)) {
            continue;
        }
        char* alter_err = nullptr;
        if (sqlite3_exec(db_, m.ddl, nullptr, nullptr, &alter_err) != SQLITE_OK) {
            const std::string msg = alter_err == nullptr ? "unknown" : alter_err;
            if (alter_err != nullptr) {
                sqlite3_free(alter_err);
            }
            log_error("database_manager",
                                std::string("plant_boxes migration failed for ") + m.name + ": " + msg);
            return false;
        }
        sqlite3_free(alter_err);
        log_info("database_manager", std::string("Applied plant_boxes migration: ") + m.name);
    }

    {
        const char* repair_default_sql =
            "UPDATE plant_boxes SET is_default = 1, min_soil_pct = ?, hysteresis_sec = ?, updated_at = ? "
            "WHERE id = (SELECT id FROM plant_boxes ORDER BY id ASC LIMIT 1) "
            "AND (SELECT COUNT(*) FROM plant_boxes WHERE is_default = 1) = 0;";
        sqlite3_stmt* repair = nullptr;
        if (sqlite3_prepare_v2(db_, repair_default_sql, -1, &repair, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(repair, 1, kDefaultPlantMinSoilPct);
            sqlite3_bind_int(repair, 2, kDefaultPlantHysteresisSec);
            const std::string repair_ts = "1970-01-01T00:00:00Z";
            bind_text(repair, 3, repair_ts);
            sqlite3_step(repair);
            sqlite3_finalize(repair);
        }

        const char* fix_unset_sql =
            "UPDATE plant_boxes SET min_soil_pct = ?, hysteresis_sec = ?, updated_at = ? "
            "WHERE is_default = 1 AND (min_soil_pct < 0 OR hysteresis_sec < 0);";
        sqlite3_stmt* fix = nullptr;
        if (sqlite3_prepare_v2(db_, fix_unset_sql, -1, &fix, nullptr) == SQLITE_OK) {
            sqlite3_bind_double(fix, 1, kDefaultPlantMinSoilPct);
            sqlite3_bind_int(fix, 2, kDefaultPlantHysteresisSec);
            const std::string fix_ts = "1970-01-01T00:00:00Z";
            bind_text(fix, 3, fix_ts);
            sqlite3_step(fix);
            sqlite3_finalize(fix);
        }
    }

    {
        const char* active_ids_sql = "SELECT id FROM plant_boxes WHERE is_active = 1 ORDER BY id ASC;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, active_ids_sql, -1, &stmt, nullptr) != SQLITE_OK) {
            log_error("database_manager", "Failed to prepare plant_boxes active id query");
            return false;
        }
        std::vector<int> active_ids;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            active_ids.push_back(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);

        if (active_ids.size() > 1) {
            const int keep_id = active_ids.front();
            const char* deactivate_sql =
                "UPDATE plant_boxes SET is_active = 0, updated_at = ? WHERE is_active = 1 AND id != ?;";
            sqlite3_stmt* u = nullptr;
            if (sqlite3_prepare_v2(db_, deactivate_sql, -1, &u, nullptr) != SQLITE_OK) {
                log_error("database_manager", "Failed to prepare plant_boxes multi-active repair");
                return false;
            }
            const std::string repair_ts = "1970-01-01T00:00:00Z";
            bind_text(u, 1, repair_ts);
            sqlite3_bind_int(u, 2, keep_id);
            if (sqlite3_step(u) != SQLITE_DONE) {
                log_error("database_manager", "Failed to repair multiple active plant_boxes");
                sqlite3_finalize(u);
                return false;
            }
            sqlite3_finalize(u);
            log_warn(
                "database_manager",
                std::string("plant_boxes had multiple active rows; kept id=") + std::to_string(keep_id) +
                    " active and deactivated the rest");
        }
    }

    const char* count_sql = "SELECT COUNT(*) FROM plant_boxes;";
    sqlite3_stmt* count_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, count_sql, -1, &count_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    int total = 0;
    if (sqlite3_step(count_stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(count_stmt, 0);
    }
    sqlite3_finalize(count_stmt);
    if (total > 0) {
        return true;
    }

    const std::string now = "1970-01-01T00:00:00Z";
    const char* seed_sql =
        "INSERT INTO plant_boxes(label, slot, is_active, is_default, min_soil_pct, max_soil_pct, pump_duration_sec, "
        "hysteresis_sec, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* seed_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, seed_sql, -1, &seed_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bind_text(seed_stmt, 1, kDefaultPlantBoxLabel);
    sqlite3_bind_int(seed_stmt, 2, 1);
    sqlite3_bind_int(seed_stmt, 3, 0);
    sqlite3_bind_int(seed_stmt, 4, 1);
    sqlite3_bind_double(seed_stmt, 5, kDefaultPlantMinSoilPct);
    sqlite3_bind_double(seed_stmt, 6, 100.0);
    sqlite3_bind_int(seed_stmt, 7, 1);
    sqlite3_bind_int(seed_stmt, 8, kDefaultPlantHysteresisSec);
    bind_text(seed_stmt, 9, now);
    bind_text(seed_stmt, 10, now);
    const bool seeded = sqlite3_step(seed_stmt) == SQLITE_DONE;
    sqlite3_finalize(seed_stmt);
    return seeded;
}

bool DatabaseManager::apply_factory_defaults() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    auto exec_locked = [this](const char* sql) {
        char* err = nullptr;
        const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            const std::string msg = err == nullptr ? "unknown" : err;
            if (err != nullptr) {
                sqlite3_free(err);
            }
            log_error("database_manager", "Factory defaults SQL failed: " + msg);
            return false;
        }
        return true;
    };

    const std::string now = now_utc_iso8601();
    if (!exec_locked("DELETE FROM profiles WHERE is_default = 0;")) {
        return false;
    }

    const std::array<Profile, 3> defaults {
        Profile {"tomato", 55.0, 75.0, 35.0, 60.0, 18.0, 26.0, 60, true},
        Profile {"cucumber", 60.0, 85.0, 45.0, 70.0, 20.0, 28.0, 60, true},
        Profile {"chili", 50.0, 70.0, 30.0, 55.0, 22.0, 30.0, 60, true},
    };
    const char* profile_sql =
        "INSERT INTO profiles(name, crop_type, min_humidity_air, max_humidity_air, "
        "min_humidity_soil, max_humidity_soil, min_temp_c, max_temp_c, interval_sec, "
        "is_default, created_at, updated_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, 1, ?, ?) "
        "ON CONFLICT(name) DO UPDATE SET crop_type=excluded.crop_type, "
        "min_humidity_air=excluded.min_humidity_air, max_humidity_air=excluded.max_humidity_air, "
        "min_humidity_soil=excluded.min_humidity_soil, max_humidity_soil=excluded.max_humidity_soil, "
        "min_temp_c=excluded.min_temp_c, max_temp_c=excluded.max_temp_c, interval_sec=excluded.interval_sec, "
        "is_default=1, updated_at=excluded.updated_at;";
    for (const auto& profile : defaults) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, profile_sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        bind_text(stmt, 1, profile.name);
        bind_text(stmt, 2, profile.name);
        sqlite3_bind_double(stmt, 3, profile.min_humidity_air);
        sqlite3_bind_double(stmt, 4, profile.max_humidity_air);
        sqlite3_bind_double(stmt, 5, profile.min_humidity_soil);
        sqlite3_bind_double(stmt, 6, profile.max_humidity_soil);
        sqlite3_bind_double(stmt, 7, profile.min_temp_c);
        sqlite3_bind_double(stmt, 8, profile.max_temp_c);
        sqlite3_bind_int(stmt, 9, profile.interval_sec);
        bind_text(stmt, 10, now);
        bind_text(stmt, 11, now);
        const bool step_ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!step_ok) {
            return false;
        }
    }

    if (!exec_locked("DELETE FROM plant_boxes WHERE is_default = 0;")) {
        return false;
    }
    if (!exec_locked("UPDATE plant_boxes SET is_active = 0;")) {
        return false;
    }

    int default_box_id = 0;
    {
        const char* find_sql = "SELECT id FROM plant_boxes WHERE is_default = 1 LIMIT 1;";
        sqlite3_stmt* find_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, find_sql, -1, &find_stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        if (sqlite3_step(find_stmt) == SQLITE_ROW) {
            default_box_id = sqlite3_column_int(find_stmt, 0);
        }
        sqlite3_finalize(find_stmt);
    }

    PlantBoxConfig box;
    if (default_box_id > 0) {
        box.id = default_box_id;
    }
    box.label = kDefaultPlantBoxLabel;
    box.slot = 1;
    box.is_active = false;
    box.is_default = true;
    box.min_soil_pct = kDefaultPlantMinSoilPct;
    box.max_soil_pct = 100.0;
    box.pump_duration_sec = 1;
    box.hysteresis_sec = kDefaultPlantHysteresisSec;

    const char* box_sql =
        "INSERT INTO plant_boxes(id, label, slot, is_active, is_default, min_soil_pct, max_soil_pct, "
        "pump_duration_sec, hysteresis_sec, created_at, updated_at) "
        "VALUES(?, ?, ?, 0, 1, ?, 100.0, 1, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET label=excluded.label, slot=excluded.slot, is_active=0, "
        "is_default=1, min_soil_pct=excluded.min_soil_pct, max_soil_pct=excluded.max_soil_pct, "
        "pump_duration_sec=excluded.pump_duration_sec, hysteresis_sec=excluded.hysteresis_sec, "
        "updated_at=excluded.updated_at;";
    if (default_box_id <= 0) {
        const char* insert_sql =
            "INSERT INTO plant_boxes(label, slot, is_active, is_default, min_soil_pct, max_soil_pct, "
            "pump_duration_sec, hysteresis_sec, created_at, updated_at) "
            "VALUES(?, ?, 0, 1, ?, 100.0, 1, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        bind_text(stmt, 1, box.label);
        sqlite3_bind_int(stmt, 2, box.slot);
        sqlite3_bind_double(stmt, 3, box.min_soil_pct);
        sqlite3_bind_int(stmt, 4, box.hysteresis_sec);
        bind_text(stmt, 5, now);
        bind_text(stmt, 6, now);
        const bool step_ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!step_ok) {
            return false;
        }
    } else {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, box_sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }
        sqlite3_bind_int(stmt, 1, box.id);
        bind_text(stmt, 2, box.label);
        sqlite3_bind_int(stmt, 3, box.slot);
        sqlite3_bind_double(stmt, 4, box.min_soil_pct);
        sqlite3_bind_int(stmt, 5, box.hysteresis_sec);
        bind_text(stmt, 6, now);
        bind_text(stmt, 7, now);
        const bool step_ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!step_ok) {
            return false;
        }
    }

    ActiveConfig cfg;
    cfg.mode = Mode::Auto;
    cfg.profile_name = "";
    cfg.interval_sec = 60;
    cfg.emergency_stop = false;
    cfg.ventilation_enabled = false;
    cfg.watering_enabled = false;

    const char* active_sql =
        "INSERT INTO active_config(id, timestamp, mode, profile_name, interval_sec, emergency_stop, "
        "ventilation_enabled, watering_enabled) "
        "VALUES(1, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET timestamp=excluded.timestamp, mode=excluded.mode, "
        "profile_name=excluded.profile_name, interval_sec=excluded.interval_sec, "
        "emergency_stop=excluded.emergency_stop, "
        "ventilation_enabled=excluded.ventilation_enabled, "
        "watering_enabled=excluded.watering_enabled;";

    sqlite3_stmt* active_stmt = nullptr;
    if (sqlite3_prepare_v2(db_, active_sql, -1, &active_stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bind_text(active_stmt, 1, now);
    bind_text(active_stmt, 2, to_string(cfg.mode));
    bind_text(active_stmt, 3, cfg.profile_name);
    sqlite3_bind_int(active_stmt, 4, cfg.interval_sec);
    sqlite3_bind_int(active_stmt, 5, cfg.emergency_stop ? 1 : 0);
    sqlite3_bind_int(active_stmt, 6, cfg.ventilation_enabled ? 1 : 0);
    sqlite3_bind_int(active_stmt, 7, cfg.watering_enabled ? 1 : 0);
    const bool active_ok = sqlite3_step(active_stmt) == SQLITE_DONE;
    sqlite3_finalize(active_stmt);

    if (active_ok) {
        log_info("database_manager", "Applied factory defaults (profiles, plant box, active config)");
    }
    return active_ok;
}

