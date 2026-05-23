
#pragma once

#include <string>
#include <string_view>

std::string now_utc_iso8601();

std::string utc_date_today();

std::string utc_iso8601_days_ago(int days);
bool is_iso8601_utc(std::string_view value);
