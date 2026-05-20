#pragma once

#include <string>
#include <string_view>

std::string now_utc_iso8601();
/** UTC calendar date YYYY-MM-DD (for daily housekeeping). */
std::string utc_date_today();
/** ISO-8601 UTC timestamp approximately `days`×24h before now (for retention cutoffs). */
std::string utc_iso8601_days_ago(int days);
bool is_iso8601_utc(std::string_view value);
