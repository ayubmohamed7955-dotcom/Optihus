#include "datahaandtering/database_manager.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include "shared/logger.hpp"

namespace datahaandtering {

namespace {
int bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

std::string col_text(sqlite3_stmt* stmt, int index) {
    const unsigned char* raw = sqlite3_column_text(stmt, index);
    return raw == nullptr ? "" : reinterpret_cast<const char*>(raw);
}
}  // namespace

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
        shared::log_error("database_manager", "sqlite3_open failed for " + db_path_);
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }
    shared::log_info("database_manager", "Opened SQLite: " + db_path_);
    return true;
}

void DatabaseManager::close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return;
    }
    sqlite3_close(db_);
    db_ = nullptr;
    shared::log_info("database_manager", "Closed SQLite");
}

bool DatabaseManager::initialize_schema(const std::string& init_sql_path) {
    std::ifstream file(init_sql_path);
    if (!file.is_open()) {
        shared::log_error("database_manager", "Could not open schema file: " + init_sql_path);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return exec_sql(buffer.str());
}

bool DatabaseManager::insert_telemetry(
    const std::string& timestamp_utc,
    const shared::TelemetrySample& sample,
    const std::string& source
) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO telemetry(timestamp, humidity_air, humidity_soil, pump_status, motor_status, "
        "error_status, source) VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, timestamp_utc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, sample.humidity_air);
    sqlite3_bind_double(stmt, 3, sample.humidity_soil);
    sqlite3_bind_text(stmt, 4, sample.pump_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, sample.motor_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, sample.error_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, source.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
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

bool DatabaseManager::upsert_profile(const shared::Profile& profile, const std::string& timestamp_utc) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO profiles(name, crop_type, min_humidity_air, max_humidity_air, min_humidity_soil, "
        "max_humidity_soil, interval_sec, is_default, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(name) DO UPDATE SET "
        "crop_type=excluded.crop_type, min_humidity_air=excluded.min_humidity_air, "
        "max_humidity_air=excluded.max_humidity_air, min_humidity_soil=excluded.min_humidity_soil, "
        "max_humidity_soil=excluded.max_humidity_soil, interval_sec=excluded.interval_sec, "
        "is_default=excluded.is_default, updated_at=excluded.updated_at;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bind_text(stmt, 1, profile.name);
    bind_text(stmt, 2, profile.crop_type);
    sqlite3_bind_double(stmt, 3, profile.min_humidity_air);
    sqlite3_bind_double(stmt, 4, profile.max_humidity_air);
    sqlite3_bind_double(stmt, 5, profile.min_humidity_soil);
    sqlite3_bind_double(stmt, 6, profile.max_humidity_soil);
    sqlite3_bind_int(stmt, 7, profile.interval_sec);
    sqlite3_bind_int(stmt, 8, profile.is_default ? 1 : 0);
    bind_text(stmt, 9, timestamp_utc);
    bind_text(stmt, 10, timestamp_utc);

    const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<shared::Profile> DatabaseManager::list_profiles() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    std::vector<shared::Profile> result;
    if (db_ == nullptr) {
        return result;
    }

    const char* sql =
        "SELECT name, crop_type, min_humidity_air, max_humidity_air, min_humidity_soil, "
        "max_humidity_soil, interval_sec, is_default FROM profiles ORDER BY name;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        shared::Profile p;
        p.name = col_text(stmt, 0);
        p.crop_type = col_text(stmt, 1);
        p.min_humidity_air = sqlite3_column_double(stmt, 2);
        p.max_humidity_air = sqlite3_column_double(stmt, 3);
        p.min_humidity_soil = sqlite3_column_double(stmt, 4);
        p.max_humidity_soil = sqlite3_column_double(stmt, 5);
        p.interval_sec = sqlite3_column_int(stmt, 6);
        p.is_default = sqlite3_column_int(stmt, 7) == 1;
        result.push_back(std::move(p));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool DatabaseManager::load_active_config(shared::ActiveConfig& out_config) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql = "SELECT mode, profile_name, interval_sec, emergency_stop FROM active_config WHERE id=1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool ok = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto mode_str = col_text(stmt, 0);
        const auto parsed = shared::mode_from_string(mode_str);
        if (parsed.has_value()) {
            out_config.mode = *parsed;
            out_config.profile_name = col_text(stmt, 1);
            out_config.interval_sec = sqlite3_column_int(stmt, 2);
            out_config.emergency_stop = sqlite3_column_int(stmt, 3) == 1;
            ok = true;
        }
    }

    sqlite3_finalize(stmt);
    return ok;
}

bool DatabaseManager::save_active_config(
    const shared::ActiveConfig& config,
    const std::string& timestamp_utc
) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    if (db_ == nullptr) {
        return false;
    }

    const char* sql =
        "INSERT INTO active_config(id, timestamp, mode, profile_name, interval_sec, emergency_stop) "
        "VALUES(1, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET timestamp=excluded.timestamp, mode=excluded.mode, "
        "profile_name=excluded.profile_name, interval_sec=excluded.interval_sec, "
        "emergency_stop=excluded.emergency_stop;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bind_text(stmt, 1, timestamp_utc);
    bind_text(stmt, 2, shared::to_string(config.mode));
    bind_text(stmt, 3, config.profile_name);
    sqlite3_bind_int(stmt, 4, config.interval_sec);
    sqlite3_bind_int(stmt, 5, config.emergency_stop ? 1 : 0);

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
        shared::log_error("database_manager", "SQL execution failed: " + error);
        return false;
    }
    return true;
}

}  // namespace datahaandtering
