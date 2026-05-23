#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

namespace optihus {


inline std::string resolve_db_path(const char* cli_override = nullptr) {
    if (cli_override != nullptr && cli_override[0] != '\0') {
        return cli_override;
    }
    if (const char* env = std::getenv("OPTIHUS_DB_PATH"); env != nullptr && env[0] != '\0') {
        return env;
    }
#if defined(_WIN32)
    return "data/optihus.db";
#else
    return "/opt/optihus/data/optihus.db";
#endif
}

inline bool ensure_parent_directories(const std::string& file_path) {
    try {
        const auto parent = std::filesystem::path(file_path).parent_path();
        if (parent.empty()) {
            return true;
        }
        std::filesystem::create_directories(parent);
        return true;
    } catch (...) {
        return false;
    }
}

}
