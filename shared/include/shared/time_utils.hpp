#pragma once

#include <string>
#include <string_view>

namespace shared {

std::string now_utc_iso8601();
bool is_iso8601_utc(std::string_view value);

}  // namespace shared
