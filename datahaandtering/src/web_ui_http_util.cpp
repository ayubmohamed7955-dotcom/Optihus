
#include "datahaandtering/web_ui_http_util.hpp"

#include <algorithm>
#include <set>
#include <sstream>

std::optional<bool> web_ui_parse_bool_query(
    const std::unordered_map<std::string, std::string>& query,
    const char* key
) {
    const auto it = query.find(key);
    if (it == query.end()) {
        return std::nullopt;
    }
    const auto& v = it->second;
    if (v == "1" || v == "true" || v == "on") {
        return true;
    }
    if (v == "0" || v == "false" || v == "off") {
        return false;
    }
    return std::nullopt;
}

std::unordered_map<std::string, std::string> web_ui_parse_query(std::string_view request_line) {
    const auto hex_value = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        return -1;
    };
    const auto url_decode = [&](std::string_view encoded) -> std::string {
        std::string out;
        out.reserve(encoded.size());
        for (std::size_t i = 0; i < encoded.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(encoded[i]);
            if (c == '+') {
                out.push_back(' ');
                continue;
            }
            if (c == '%' && (i + 2) < encoded.size()) {
                const int hi = hex_value(static_cast<unsigned char>(encoded[i + 1]));
                const int lo = hex_value(static_cast<unsigned char>(encoded[i + 2]));
                if (hi >= 0 && lo >= 0) {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            out.push_back(static_cast<char>(c));
        }
        return out;
    };

    std::unordered_map<std::string, std::string> out;
    const std::size_t q = request_line.find('?');
    if (q == std::string_view::npos) {
        return out;
    }
    const std::size_t end = request_line.find(' ', q);
    if (end == std::string_view::npos || end <= q + 1) {
        return out;
    }
    const std::string query(request_line.substr(q + 1, end - (q + 1)));
    std::size_t start = 0;
    while (start < query.size()) {
        const std::size_t amp = query.find('&', start);
        const std::size_t item_end = (amp == std::string::npos) ? query.size() : amp;
        const std::size_t eq = query.find('=', start);
        if (eq != std::string::npos && eq < item_end) {
            const std::string raw_key = query.substr(start, eq - start);
            const std::string raw_value = query.substr(eq + 1, item_end - (eq + 1));
            out[url_decode(raw_key)] = url_decode(raw_value);
        }
        start = item_end + 1;
    }
    return out;
}

std::string web_ui_json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::vector<std::string> web_ui_split_csv(const std::string& value) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = (comma == std::string::npos) ? value.size() : comma;
        std::string item = value.substr(start, end - start);
        const auto trim_left = item.find_first_not_of(" \t");
        const auto trim_right = item.find_last_not_of(" \t");
        if (trim_left != std::string::npos && trim_right != std::string::npos) {
            item = item.substr(trim_left, trim_right - trim_left + 1);
            if (!item.empty()) {
                out.push_back(item);
            }
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

std::vector<std::string> web_ui_dedupe_preserve_order(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& item : items) {
        if (seen.insert(item).second) {
            out.push_back(item);
        }
    }
    return out;
}

std::string web_ui_join_csv(const std::vector<std::string>& items) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < items.size(); ++i) {
        oss << items[i];
        if (i + 1 < items.size()) {
            oss << ",";
        }
    }
    return oss.str();
}

bool web_ui_profile_csv_sets_equal(const std::string& a, const std::string& b) {
    auto va = web_ui_dedupe_preserve_order(web_ui_split_csv(a));
    auto vb = web_ui_dedupe_preserve_order(web_ui_split_csv(b));
    std::sort(va.begin(), va.end());
    std::sort(vb.begin(), vb.end());
    return va == vb;
}

void web_ui_migrate_profile_name_in_active_config(
    ConfigManager& config_manager,
    const std::string& old_name,
    const std::string& new_name
) {
    if (old_name.empty() || old_name == new_name) {
        return;
    }
    const auto cfg = config_manager.current_config();
    auto names = web_ui_dedupe_preserve_order(web_ui_split_csv(cfg.profile_name));
    bool changed = false;
    for (auto& n : names) {
        if (n == old_name) {
            n = new_name;
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    names = web_ui_dedupe_preserve_order(names);
    config_manager.set_profile_name(web_ui_join_csv(names));
    config_manager.save_active_config();
}

bool web_ui_aggregate_bounds_if_compatible(
    const std::vector<Profile>& selected_profiles,
    WebUiAggregatedBounds& out_bounds
) {
    if (selected_profiles.empty()) {
        return false;
    }
    out_bounds.min_air = selected_profiles.front().min_humidity_air;
    out_bounds.max_air = selected_profiles.front().max_humidity_air;
    out_bounds.min_soil = selected_profiles.front().min_humidity_soil;
    out_bounds.max_soil = selected_profiles.front().max_humidity_soil;

    for (std::size_t i = 1; i < selected_profiles.size(); ++i) {
        const auto& p = selected_profiles[i];
        out_bounds.min_air = std::max(out_bounds.min_air, p.min_humidity_air);
        out_bounds.max_air = std::min(out_bounds.max_air, p.max_humidity_air);
        out_bounds.min_soil = std::max(out_bounds.min_soil, p.min_humidity_soil);
        out_bounds.max_soil = std::min(out_bounds.max_soil, p.max_humidity_soil);
    }
    return out_bounds.min_air <= out_bounds.max_air && out_bounds.min_soil <= out_bounds.max_soil;
}
