#include "shared/logger.hpp"

#include <iostream>
#include <mutex>
#include <string_view>

#include "shared/time_utils.hpp"

static std::mutex g_log_mutex;
static LogLevel g_min_log_level = LogLevel::Info;

static std::string_view level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

void set_min_log_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_min_log_level = level;
}

void log(LogLevel level, std::string_view component, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (static_cast<int>(level) < static_cast<int>(g_min_log_level)) {
        return;
    }
    std::cout << "[" << now_utc_iso8601() << "]"
              << "[" << level_to_string(level) << "]"
              << "[" << component << "] " << message << std::endl;
}

void log_info(std::string_view component, std::string_view message) {
    log(LogLevel::Info, component, message);
}

void log_warn(std::string_view component, std::string_view message) {
    log(LogLevel::Warn, component, message);
}

void log_error(std::string_view component, std::string_view message) {
    log(LogLevel::Error, component, message);
}
