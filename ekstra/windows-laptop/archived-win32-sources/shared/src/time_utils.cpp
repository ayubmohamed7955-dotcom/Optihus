#include "shared/time_utils.hpp"

#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

std::tm utc_tm_from_time(std::time_t time_now) {
    std::tm utc_tm {};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &time_now);
#else
    gmtime_r(&time_now, &utc_tm);
#endif
    return utc_tm;
}

}

std::string now_utc_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time_now = std::chrono::system_clock::to_time_t(now);
    const std::tm utc_tm = utc_tm_from_time(time_now);

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string utc_date_today() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time_now = std::chrono::system_clock::to_time_t(now);
    const std::tm utc_tm = utc_tm_from_time(time_now);

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%d");
    return oss.str();
}

std::string utc_iso8601_days_ago(int days) {
    if (days < 0) {
        days = 0;
    }
    const auto now = std::chrono::system_clock::now();
    const auto cutoff = now - std::chrono::hours(24 * days);
    const std::time_t time_cutoff = std::chrono::system_clock::to_time_t(cutoff);
    const std::tm utc_tm = utc_tm_from_time(time_cutoff);

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool is_iso8601_utc(std::string_view value) {

    if (value.size() != 20) {
        return false;
    }
    const auto is_digit_at = [&value](std::size_t index) {
        return std::isdigit(static_cast<unsigned char>(value[index])) != 0;
    };

    for (const std::size_t idx : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 11u, 12u, 14u, 15u, 17u, 18u}) {
        if (!is_digit_at(idx)) {
            return false;
        }
    }

    if (value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
        value[16] != ':' || value[19] != 'Z') {
        return false;
    }

    return true;
}
