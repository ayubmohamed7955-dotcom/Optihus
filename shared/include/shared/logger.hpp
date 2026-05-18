#pragma once

#include <string>
#include <string_view>

enum class LogLevel {
    Info,
    Warn,
    Error
};

void set_min_log_level(LogLevel level);
void log(LogLevel level, std::string_view component, std::string_view message);
void log_info(std::string_view component, std::string_view message);
void log_warn(std::string_view component, std::string_view message);
void log_error(std::string_view component, std::string_view message);
