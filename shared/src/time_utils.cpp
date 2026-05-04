#include "shared/time_utils.hpp"

#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace shared {

std::string now_utc_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time_now = std::chrono::system_clock::to_time_t(now);

    std::tm utc_tm {};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &time_now);
#else
    gmtime_r(&time_now, &utc_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool is_iso8601_utc(std::string_view value) {
    // Expected format: YYYY-MM-DDTHH:MM:SSZ
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

}  // namespace shared
