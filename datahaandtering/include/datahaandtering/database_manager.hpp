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
    std::vector<shared::Profile> list_profiles();

    bool load_active_config(shared::ActiveConfig& out_config);
    bool save_active_config(const shared::ActiveConfig& config, const std::string& timestamp_utc);

    const std::string& db_path() const;

private:
    bool exec_sql(const std::string& sql);

    std::string db_path_;
    sqlite3* db_ {nullptr};
    std::mutex db_mutex_;
};

}  // namespace datahaandtering
