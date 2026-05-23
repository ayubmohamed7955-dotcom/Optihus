
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "shared/message.hpp"

std::string encode_json_line(const MessageEnvelope& message);
std::optional<MessageEnvelope> decode_json_line(std::string_view line);
