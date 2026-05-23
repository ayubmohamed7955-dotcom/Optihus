
#pragma once

#include "datahaandtering/config_manager.hpp"
#include "shared/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

std::optional<bool> web_ui_parse_bool_query(
    const std::unordered_map<std::string, std::string>& query,
    const char* key
);

std::unordered_map<std::string, std::string> web_ui_parse_query(std::string_view request_line);

std::string web_ui_json_escape(const std::string& s);

std::vector<std::string> web_ui_split_csv(const std::string& value);

std::vector<std::string> web_ui_dedupe_preserve_order(const std::vector<std::string>& items);

std::string web_ui_join_csv(const std::vector<std::string>& items);

bool web_ui_profile_csv_sets_equal(const std::string& a, const std::string& b);

struct WebUiAggregatedBounds {
    double min_air {0.0};
    double max_air {0.0};
    double min_soil {0.0};
    double max_soil {0.0};
};

void web_ui_migrate_profile_name_in_active_config(
    ConfigManager& config_manager,
    const std::string& old_name,
    const std::string& new_name
);

bool web_ui_aggregate_bounds_if_compatible(
    const std::vector<Profile>& selected_profiles,
    WebUiAggregatedBounds& out_bounds
);
