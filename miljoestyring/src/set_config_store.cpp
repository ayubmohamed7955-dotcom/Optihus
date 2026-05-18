#include "set_config_store.hpp"

#include "shared/logger.hpp"
#include "shared/time_utils.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <utility>

namespace miljoestyring {
namespace {

constexpr int kFileFormatVersion = 2;

std::filesystem::path state_directory() {
    if (const char* env = std::getenv("OPTIHUS_STATE_DIR")) {
        return std::filesystem::path(env);
    }
#if defined(_WIN32)
    return std::filesystem::current_path() / "optihus_state";
#else
    return std::filesystem::path("/var/lib/optihus");
#endif
}

std::filesystem::path state_file_path() {
    return state_directory() / "miljoestyring_field_config.json";
}

std::optional<nlohmann::json> load_document_raw() {
    try {
        const auto path = state_file_path();
        if (!std::filesystem::exists(path)) {
            return std::nullopt;
        }
        std::ifstream in(path);
        if (!in.is_open()) {
            return std::nullopt;
        }
        nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        if (!doc.is_object()) {
            return std::nullopt;
        }
        return doc;
    } catch (...) {
        return std::nullopt;
    }
}

bool save_document(const nlohmann::json& doc) {
    try {
        const auto dir = state_directory();
        std::filesystem::create_directories(dir);
        const auto path = state_file_path();
        const auto tmp = path.string() + ".tmp";
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                log_warn(
                    "set_config_store",
                    "Kunne ikke skrive midlertidig state-fil: " + tmp
                );
                return false;
            }
            out << doc.dump(2);
            out.flush();
        }
        std::filesystem::rename(tmp, path);
        return true;
    } catch (const std::exception& ex) {
        log_warn(
            "set_config_store",
            std::string("Kunne ikke gemme feltkonfiguration: ") + ex.what()
        );
        return false;
    } catch (...) {
        log_warn("set_config_store", "Kunne ikke gemme feltkonfiguration (ukendt fejl)");
        return false;
    }
}

} // namespace

bool persist_field_config_after_set_config(const nlohmann::json& params, bool emergency_halt_runtime) {
    nlohmann::json doc;
    doc["version"] = kFileFormatVersion;
    doc["saved_at_utc"] = now_utc_iso8601();
    doc["params"] = params;
    doc["emergency_halt"] = emergency_halt_runtime;
    const bool ok = save_document(doc);
    if (ok) {
        log_info(
            "set_config_store",
            "Gemte sidste set_config til " + state_file_path().string()
        );
    }
    return ok;
}

bool persist_field_config_emergency_overlay(bool emergency_halt_runtime) {
    const auto existing = load_document_raw();
    if (!existing.has_value()) {
        return false;
    }
    nlohmann::json doc = *existing;
    doc["version"] = kFileFormatVersion;
    doc["saved_at_utc"] = now_utc_iso8601();
    doc["emergency_halt"] = emergency_halt_runtime;
    return save_document(doc);
}

std::optional<nlohmann::json> load_field_config_for_replay() {
    const auto doc = load_document_raw();
    if (!doc.has_value()) {
        return std::nullopt;
    }
    const int ver = doc->value("version", 0);
    if (ver != kFileFormatVersion) {
        log_warn(
            "set_config_store",
            "Ukendt version i feltkonfigurationsfil — springer replay over"
        );
        return std::nullopt;
    }
    const auto& params = (*doc)["params"];
    if (!params.is_object() || params.empty()) {
        log_warn(
            "set_config_store",
            "Tom eller ugyldig params i feltkonfigurationsfil — springer replay over"
        );
        return std::nullopt;
    }
    nlohmann::json merged = params;
    merged["emergency_stop"] = doc->value("emergency_halt", merged.value("emergency_stop", false));
    return merged;
}

} // namespace miljoestyring
