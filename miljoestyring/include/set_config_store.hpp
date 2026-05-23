
#pragma once

#include <nlohmann/json.hpp>
#include <optional>

namespace miljoestyring {






bool persist_field_config_after_set_config(const nlohmann::json& params, bool emergency_halt_runtime);


bool persist_field_config_emergency_overlay(bool emergency_halt_runtime);


std::optional<nlohmann::json> load_field_config_for_replay();

}
