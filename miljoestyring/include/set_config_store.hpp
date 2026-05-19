#pragma once

#include <nlohmann/json.hpp>
#include <optional>

namespace miljoestyring {

/**
 * Gemmer sidste komplette set_config-params og aktuel nødstop-tilstand (Pi reboot persistence).
 * Standardsti: Linux `~/.local/share/optihus/miljoestyring_field_config.json` (eller `./optihus_state/` uden HOME),
 * Windows `./optihus_state/`. Overstyres med miljøvariabel OPTIHUS_STATE_DIR (fx `/var/lib/optihus`).
 */
bool persist_field_config_after_set_config(const nlohmann::json& params, bool emergency_halt_runtime);

/** Opdaterer kun emergency_halt i eksisterende fil (ingen tidligere fil ⇒ no-op). */
bool persist_field_config_emergency_overlay(bool emergency_halt_runtime);

/** Til boot: merged params med emergency_stop synket til lagret overlay. Tom ⇒ ingen replay. */
std::optional<nlohmann::json> load_field_config_for_replay();

} // namespace miljoestyring
