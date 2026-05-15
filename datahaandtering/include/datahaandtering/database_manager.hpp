#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "shared/types.hpp"

namespace datahaandtering {

class DatabaseManager {
public:
    explicit DatabaseManager(std::string db_path);
    ~DatabaseManager();

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool open();
    void close();

    bool initialize_schema(const std::string& init_sql_path);
    bool insert_telemetry(
        const std::string& timestamp_utc,
        const shared::TelemetrySample& sample,
        const std::string& source
    );
    bool insert_system_event(
        const std::string& timestamp_utc,
        const std::string& event_type,
        const std::string& description,
        const std::string& severity
    );
    bool insert_command_history(
        const std::string& timestamp_utc,
        const std::string& action,
        const std::string& payload_json,
        const std::string& source,
        const std::string& status
    );
    bool upsert_profile(const shared::Profile& profile, const std::string& timestamp_utc);
    bool delete_profile(const std::string& profile_name);
    std::vector<shared::Profile> list_profiles();
    bool upsert_plant_box(const shared::PlantBoxConfig& plant_box, const std::string& timestamp_utc);
    bool delete_plant_box(int id);
    std::vector<shared::PlantBoxConfig> list_plant_boxes();

    bool load_active_config(shared::ActiveConfig& out_config);
    bool save_active_config(const shared::ActiveConfig& config, const std::string& timestamp_utc);
    /// ISO-8601 timestamp for row id=1, or empty if missing.
    std::string read_active_config_timestamp_utc() const;

    const std::string& db_path() const;

private:
    bool exec_sql(const std::string& sql);
    bool ensure_plant_boxes_schema_locked();

    std::string db_path_;
    sqlite3* db_ {nullptr};
    mutable std::mutex db_mutex_;
};

}  // namespace datahaandtering
