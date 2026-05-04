#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "shared/message.hpp"

namespace shared {

std::string encode_json_line(const MessageEnvelope& message);
std::optional<MessageEnvelope> decode_json_line(std::string_view line);

}  // namespace shared
