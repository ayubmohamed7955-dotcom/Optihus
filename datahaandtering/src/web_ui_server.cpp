#include "datahaandtering/web_ui_server.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "shared/logger.hpp"
#include "shared/types.hpp"
#include "shared/time_utils.hpp"

#if defined(_WIN32)
static std::string last_socket_error() {
    return std::to_string(WSAGetLastError());
}
#else
static std::string last_socket_error() {
    return std::strerror(errno);
}
#endif

static void close_socket(int socket_fd) {
#if defined(_WIN32)
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

static std::optional<bool> parse_bool_query(
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

static std::unordered_map<std::string, std::string> parse_query(std::string_view request_line) {
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

static std::string json_escape(const std::string& s) {
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

static std::vector<std::string> split_csv(const std::string& value) {
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

static std::vector<std::string> dedupe_preserve_order(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& item : items) {
        if (seen.insert(item).second) {
            out.push_back(item);
        }
    }
    return out;
}

static std::string join_csv(const std::vector<std::string>& items) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < items.size(); ++i) {
        oss << items[i];
        if (i + 1 < items.size()) {
            oss << ",";
        }
    }
    return oss.str();
}

static bool profile_csv_sets_equal(const std::string& a, const std::string& b) {
    auto va = dedupe_preserve_order(split_csv(a));
    auto vb = dedupe_preserve_order(split_csv(b));
    std::sort(va.begin(), va.end());
    std::sort(vb.begin(), vb.end());
    return va == vb;
}

static std::set<std::string> profile_name_set(const std::vector<Profile>& profiles) {
    std::set<std::string> names;
    for (const auto& p : profiles) {
        names.insert(p.name);
    }
    return names;
}

struct AggregatedBounds {
    double min_air {0.0};
    double max_air {0.0};
    double min_soil {0.0};
    double max_soil {0.0};
};

static void migrate_profile_name_in_active_config(
    ConfigManager& config_manager,
    const std::string& old_name,
    const std::string& new_name
) {
    if (old_name.empty() || old_name == new_name) {
        return;
    }
    const auto cfg = config_manager.current_config();
    auto names = dedupe_preserve_order(split_csv(cfg.profile_name));
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
    names = dedupe_preserve_order(names);
    config_manager.set_profile_name(join_csv(names));
    config_manager.save_active_config();
}

static bool aggregate_bounds_if_compatible(
    const std::vector<Profile>& selected_profiles,
    AggregatedBounds& out_bounds
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

WebUiServer::WebUiServer(
    int port,
    TcpServer& tcp_server,
    CommandDispatcher& command_dispatcher,
    ConfigManager& config_manager,
    DatabaseManager& db_manager
)
    : port_(port),
      tcp_server_(tcp_server),
      command_dispatcher_(command_dispatcher),
      config_manager_(config_manager),
      db_manager_(db_manager) {}

WebUiServer::~WebUiServer() {
    stop();
}

bool WebUiServer::start() {
    if (port_ <= 0 || running_.exchange(true)) {
        return false;
    }

#if defined(_WIN32)
    WSADATA wsa_data {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        log_error("web_ui", "WSAStartup failed");
        running_ = false;
        return false;
    }
#endif

    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ < 0) {
        log_error("web_ui", "Failed to create socket: " + last_socket_error());
#if defined(_WIN32)
        WSACleanup();
#endif
        running_ = false;
        return false;
    }

    int reuse = 1;
#if defined(_WIN32)
    setsockopt(
        listen_socket_,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse)
    );
#else
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        log_error("web_ui", "bind failed: " + last_socket_error());
        close_socket(listen_socket_);
        listen_socket_ = -1;
#if defined(_WIN32)
        WSACleanup();
#endif
        running_ = false;
        return false;
    }

    if (listen(listen_socket_, 8) < 0) {
        log_error("web_ui", "listen failed: " + last_socket_error());
        close_socket(listen_socket_);
        listen_socket_ = -1;
#if defined(_WIN32)
        WSACleanup();
#endif
        running_ = false;
        return false;
    }

    accept_thread_ = std::thread(&WebUiServer::accept_loop, this);
    log_info("web_ui", "Browser UI listening on port " + std::to_string(port_));
    return true;
}

void WebUiServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (listen_socket_ >= 0) {
#if defined(_WIN32)
        shutdown(listen_socket_, SD_BOTH);
#else
        shutdown(listen_socket_, SHUT_RDWR);
#endif
        close_socket(listen_socket_);
        listen_socket_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

#if defined(_WIN32)
    WSACleanup();
#endif
    log_info("web_ui", "Browser UI stopped");
}

void WebUiServer::note_client_hello(const std::string& timestamp_utc) {
    hello_count_.fetch_add(1);
    std::lock_guard<std::mutex> lock(status_mutex_);
    last_hello_ts_ = timestamp_utc;
}

void WebUiServer::note_field_live(std::string components_json, std::string telemetry_ts_utc) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    last_field_live_components_json_ = std::move(components_json);
    last_field_live_ts_ = std::move(telemetry_ts_utc);
}

void WebUiServer::accept_loop() {
    while (running_) {
        sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        const int client_socket =
            accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) {
            if (running_) {
                log_warn("web_ui", "accept failed: " + last_socket_error());
            }
            continue;
        }
        handle_client(client_socket);
        close_socket(client_socket);
    }
}

void WebUiServer::handle_client(int client_socket) {
    std::array<char, 2048> rx {};
    const int n = recv(client_socket, rx.data(), static_cast<int>(rx.size()), 0);
    if (n <= 0) {
        return;
    }

    const std::string request(rx.data(), static_cast<std::size_t>(n));
    const std::size_t line_end = request.find("\r\n");
    const std::string request_line = request.substr(0, line_end);

    std::string body;
    std::string content_type = "application/json; charset=utf-8";
    std::string status_line = "HTTP/1.1 200 OK\r\n";
    if (request_line.rfind("GET /api/status ", 0) == 0) {
        body = build_status_json();
    } else if (request_line.rfind("GET /api/config ", 0) == 0
        || request_line.rfind("POST /api/config", 0) == 0) {
        handle_config_request(request_line, status_line, body, content_type);
    } else if (request_line.rfind("GET /api/profiles ", 0) == 0
        || request_line.rfind("POST /api/profiles/upsert", 0) == 0
        || request_line.rfind("POST /api/profiles/delete", 0) == 0) {
        handle_profiles_request(request_line, status_line, body, content_type);
    } else if (request_line.rfind("GET /api/plant-boxes ", 0) == 0
        || request_line.rfind("POST /api/plant-boxes/upsert", 0) == 0
        || request_line.rfind("POST /api/plant-boxes/delete", 0) == 0) {
        handle_plant_boxes_request(request_line, status_line, body, content_type);
    } else if (request_line.rfind("POST /api/control/", 0) == 0) {
        if (!handle_control_request(request_line, status_line, body, content_type)) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid control request\"}\n";
        }
    } else if (request_line.rfind("GET / ", 0) == 0) {
        body = build_index_html();
        content_type = "text/html; charset=utf-8";
    } else {
        status_line = "HTTP/1.1 404 Not Found\r\n";
        body = "{\"error\":\"not found\"}\n";
    }

    const std::string response =
        status_line + "Content-Type: " + content_type + "\r\n"
        + "Content-Length: " + std::to_string(body.size()) + "\r\n"
        + "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        + "Pragma: no-cache\r\n"
        + "Expires: 0\r\n"
        + "Connection: close\r\n\r\n" + body;
    send_all(client_socket, response);
}

bool WebUiServer::handle_control_request(
    const std::string& request_line,
    std::string& status_line,
    std::string& body,
    std::string& content_type
) {
    content_type = "application/json; charset=utf-8";
    if (request_line.rfind("POST /api/control/emergency-stop", 0) == 0) {
        const bool enabled = request_line.find("enabled=1") != std::string::npos
            || request_line.find("enabled=true") != std::string::npos;
        const bool queued = command_dispatcher_.dispatch_emergency_stop(enabled);
        const std::string action = enabled ? "emergency_stop" : "clear_emergency_stop";
        note_control_result(action, queued);
        body = std::string("{\"ok\":true,\"action\":\"")
            + action
            + "\",\"queued\":"
            + (queued ? "true" : "false")
            + "}\n";
        return true;
    }
    if (request_line.rfind("POST /api/control/sync-config ", 0) == 0) {
        const bool queued = command_dispatcher_.dispatch_current_config();
        note_control_result("set_config", queued);
        body = std::string("{\"ok\":true,\"action\":\"set_config\",\"queued\":")
            + (queued ? "true" : "false")
            + "}\n";
        return true;
    }

    status_line = "HTTP/1.1 404 Not Found\r\n";
    body = "{\"error\":\"unknown control endpoint\"}\n";
    return true;
}

bool WebUiServer::handle_config_request(
    const std::string& request_line,
    std::string& status_line,
    std::string& body,
    std::string& content_type
) {
    content_type = "application/json; charset=utf-8";
    if (request_line.rfind("GET /api/config ", 0) == 0) {
        body = build_config_json();
        return true;
    }

    if (request_line.rfind("POST /api/config", 0) == 0) {
        const auto query = parse_query(request_line);
        bool changed = false;
        if (const auto it = query.find("profiles"); it != query.end()) {
            const auto selected = dedupe_preserve_order(split_csv(it->second));
            if (selected.empty()) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"at least one profile must be selected\"}\n";
                return true;
            }
            const auto available_profiles = db_manager_.list_profiles();
            const auto available_names = profile_name_set(available_profiles);
            if (available_names.empty()) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"no profiles exist in database\"}\n";
                return true;
            }
            for (const auto& name : selected) {
                if (available_names.find(name) == available_names.end()) {
                    status_line = "HTTP/1.1 400 Bad Request\r\n";
                    body = "{\"error\":\"unknown profile: " + json_escape(name) + "\"}\n";
                    return true;
                }
            }
            std::vector<Profile> selected_profiles;
            selected_profiles.reserve(selected.size());
            for (const auto& name : selected) {
                for (const auto& p : available_profiles) {
                    if (p.name == name) {
                        selected_profiles.push_back(p);
                        break;
                    }
                }
            }
            AggregatedBounds bounds;
            if (!aggregate_bounds_if_compatible(selected_profiles, bounds)) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"selected profiles are not compatible (no overlap in humidity ranges)\"}\n";
                return true;
            }
            config_manager_.set_profile_name(join_csv(selected));
            changed = true;
        } else if (const auto it = query.find("profile"); it != query.end() && !it->second.empty()) {
            const auto available_profiles = db_manager_.list_profiles();
            const auto available_names = profile_name_set(available_profiles);
            if (available_names.find(it->second) == available_names.end()) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"unknown profile: " + json_escape(it->second) + "\"}\n";
                return true;
            }
            config_manager_.set_profile_name(it->second);
            changed = true;
        }
        if (const auto vent = parse_bool_query(query, "ventilation_enabled"); vent.has_value()) {
            config_manager_.set_ventilation_enabled(*vent);
            changed = true;
        }
        if (const auto water = parse_bool_query(query, "watering_enabled"); water.has_value()) {
            config_manager_.set_watering_enabled(*water);
            changed = true;
        }
        if (changed) {
            config_manager_.set_interval_sec(60);
            const bool saved = config_manager_.save_active_config();
            const bool queued = command_dispatcher_.dispatch_current_config();
            note_control_result("set_config", queued);
            body = std::string("{\"ok\":")
                + (saved ? "true" : "false")
                + ",\"saved\":"
                + (saved ? "true" : "false")
                + ",\"queued\":"
                + (queued ? "true" : "false")
                + "}\n";
            return true;
        }
        status_line = "HTTP/1.1 400 Bad Request\r\n";
        body = "{\"error\":\"no valid config fields\"}\n";
        return true;
    }

    status_line = "HTTP/1.1 405 Method Not Allowed\r\n";
    body = "{\"error\":\"method not allowed\"}\n";
    return true;
}

bool WebUiServer::handle_profiles_request(
    const std::string& request_line,
    std::string& status_line,
    std::string& body,
    std::string& content_type
) {
    content_type = "application/json; charset=utf-8";
    if (request_line.rfind("GET /api/profiles ", 0) == 0) {
        body = build_profiles_json();
        return true;
    }
    if (request_line.rfind("POST /api/profiles/upsert", 0) == 0) {
        const auto query = parse_query(request_line);
        Profile p;
        if (const auto it = query.find("name"); it != query.end()) {
            p.name = it->second;
        }
        if (p.name.empty()) {
            if (const auto it = query.find("crop"); it != query.end()) {
                p.name = it->second;
            }
        }
        if (p.name.empty()) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"name required\"}\n";
            return true;
        }
        try {
            const auto clamp_profile_pct = [](double v) -> double {
                if (!std::isfinite(v)) {
                    return 0.0;
                }
                if (v < 0.0) {
                    v = 0.0;
                }
                if (v > 99.0) {
                    v = 99.0;
                }
                return std::round(v * 10.0) / 10.0;
            };
            p.min_humidity_air = clamp_profile_pct(std::stod(query.at("min_air")));
            p.max_humidity_air = clamp_profile_pct(std::stod(query.at("max_air")));
            p.min_humidity_soil = clamp_profile_pct(std::stod(query.at("min_soil")));
            p.max_humidity_soil = clamp_profile_pct(std::stod(query.at("max_soil")));
            const auto clamp_profile_temp = [](double v) -> double {
                if (!std::isfinite(v)) {
                    return 18.0;
                }
                if (v < -10.0) {
                    v = -10.0;
                }
                if (v > 50.0) {
                    v = 50.0;
                }
                return std::round(v * 10.0) / 10.0;
            };
            p.min_temp_c = clamp_profile_temp(std::stod(query.at("min_temp")));
            p.max_temp_c = clamp_profile_temp(std::stod(query.at("max_temp")));
            if (p.min_temp_c > p.max_temp_c) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"min_temp must be <= max_temp\"}\n";
                return true;
            }
            p.interval_sec = 60;
            if (const auto it = query.find("interval"); it != query.end() && !it->second.empty()) {
                p.interval_sec = std::stoi(it->second);
            }
        } catch (...) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid numeric profile field\"}\n";
            return true;
        }

        std::string replace_from;
        if (const auto it = query.find("replace_from"); it != query.end()) {
            replace_from = it->second;
        }

        const auto all_profiles = db_manager_.list_profiles();
        const auto find_profile = [&](const std::string& n) -> const Profile* {
            for (const auto& x : all_profiles) {
                if (x.name == n) {
                    return &x;
                }
            }
            return nullptr;
        };

        if (!replace_from.empty()) {
            const auto* old_p = find_profile(replace_from);
            if (old_p == nullptr) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"replace_from profile not found\"}\n";
                return true;
            }
            p.is_default = old_p->is_default;
            if (p.name != replace_from) {
                if (find_profile(p.name) != nullptr) {
                    status_line = "HTTP/1.1 400 Bad Request\r\n";
                    body = "{\"error\":\"profile name already exists\"}\n";
                    return true;
                }
                if (!db_manager_.delete_profile(replace_from)) {
                    body = "{\"ok\":false}\n";
                    return true;
                }
                const bool up = db_manager_.upsert_profile(p, now_utc_iso8601());
                if (!up) {
                    body = "{\"ok\":false,\"error\":\"upsert failed after delete\"}\n";
                    return true;
                }
                migrate_profile_name_in_active_config(config_manager_, replace_from, p.name);
                body = "{\"ok\":true,\"replaced_profile\":true}\n";
                return true;
            }
        } else {
            if (const auto* existing = find_profile(p.name)) {
                p.is_default = existing->is_default;
            } else {
                p.is_default = false;
            }
        }

        const bool ok = db_manager_.upsert_profile(p, now_utc_iso8601());
        body = std::string("{\"ok\":") + (ok ? "true" : "false") + "}\n";
        return true;
    }
    if (request_line.rfind("POST /api/profiles/delete", 0) == 0) {
        const auto query = parse_query(request_line);
        const auto it = query.find("name");
        if (it == query.end() || it->second.empty()) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"name required\"}\n";
            return true;
        }
        const auto profiles = db_manager_.list_profiles();
        for (const auto& profile : profiles) {
            if (profile.name == it->second && profile.is_default) {
                status_line = "HTTP/1.1 403 Forbidden\r\n";
                body = "{\"error\":\"default profiles cannot be deleted\"}\n";
                return true;
            }
        }
        const bool ok = db_manager_.delete_profile(it->second);
        if (ok) {
            const auto remaining_profiles = db_manager_.list_profiles();
            auto selected = dedupe_preserve_order(split_csv(config_manager_.current_config().profile_name));
            selected.erase(
                std::remove(selected.begin(), selected.end(), it->second),
                selected.end()
            );
            if (!selected.empty()) {
                config_manager_.set_profile_name(join_csv(selected));
                config_manager_.save_active_config();
            } else if (!remaining_profiles.empty()) {
                config_manager_.set_profile_name(remaining_profiles.front().name);
                config_manager_.save_active_config();
            }
        }
        body = std::string("{\"ok\":") + (ok ? "true" : "false") + "}\n";
        return true;
    }
    status_line = "HTTP/1.1 405 Method Not Allowed\r\n";
    body = "{\"error\":\"method not allowed\"}\n";
    return true;
}

bool WebUiServer::handle_plant_boxes_request(
    const std::string& request_line,
    std::string& status_line,
    std::string& body,
    std::string& content_type
) {
    content_type = "application/json; charset=utf-8";
    if (request_line.rfind("GET /api/plant-boxes ", 0) == 0) {
        body = build_plant_boxes_json();
        return true;
    }
    if (request_line.rfind("POST /api/plant-boxes/upsert", 0) == 0) {
        const auto query = parse_query(request_line);
        PlantBoxConfig plant_box;
        if (const auto it = query.find("id"); it != query.end() && !it->second.empty()) {
            try {
                plant_box.id = std::stoi(it->second);
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid id\"}\n";
                return true;
            }
        }
        if (const auto it = query.find("label"); it != query.end()) {
            plant_box.label = it->second;
        }
        if (const auto it = query.find("slot"); it != query.end()) {
            try {
                plant_box.slot = std::stoi(it->second);
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid slot\"}\n";
                return true;
            }
        }
        if (const auto it = query.find("is_active"); it != query.end()) {
            plant_box.is_active = (it->second == "1" || it->second == "true");
        }

        const auto query_nonempty = [&query](const char* key) -> bool {
            const auto it = query.find(key);
            return it != query.end() && !it->second.empty();
        };

        bool have_min_soil = false;
        bool have_hyst = false;
        double min_soil_val = 0.0;
        int hyst_val = 0;

        if (query_nonempty("min_soil_pct")) {
            try {
                min_soil_val = std::stod(query.at("min_soil_pct"));
                have_min_soil = true;
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid min_soil_pct\"}\n";
                return true;
            }
        }
        if (query_nonempty("hysteresis_sec")) {
            try {
                hyst_val = std::stoi(query.at("hysteresis_sec"));
                have_hyst = true;
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid hysteresis_sec\"}\n";
                return true;
            }
        }

        if (plant_box.label.empty() || plant_box.slot < 1) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"label and slot>=1 required\"}\n";
            return true;
        }

        if (plant_box.id > 0) {
            std::optional<PlantBoxConfig> existing;
            for (const auto& b : db_manager_.list_plant_boxes()) {
                if (b.id == plant_box.id) {
                    existing = b;
                    break;
                }
            }
            if (!existing.has_value()) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"unknown plant box id\"}\n";
                return true;
            }
            plant_box.min_soil_pct = have_min_soil ? min_soil_val : existing->min_soil_pct;
            plant_box.hysteresis_sec = have_hyst ? hyst_val : existing->hysteresis_sec;
        } else {
            if (have_min_soil) {
                plant_box.min_soil_pct = min_soil_val;
            }
            if (have_hyst) {
                plant_box.hysteresis_sec = hyst_val;
            }
        }

        if (!plant_box_watering_params_configured(plant_box)) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body =
                "{\"error\":\"Udfyld minimum jordfugtighed (%) og pause mellem vandinger (sek.). "
                "Ufuldstændige profiler gemmes ikke og sendes ikke til Pi. Pumpe: fast 1 s (~25 ml) i feltet.\"}\n";
            return true;
        }
        if (plant_box.min_soil_pct < 0.0 || plant_box.min_soil_pct > 100.0) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid min_soil_pct (expect 0..100)\"}\n";
            return true;
        }
        plant_box.max_soil_pct = 100.0;
        plant_box.pump_duration_sec = 1;
        if (plant_box.hysteresis_sec < 0 || plant_box.hysteresis_sec > 86400) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid hysteresis_sec (expect 0..86400)\"}\n";
            return true;
        }

        const bool ok = db_manager_.upsert_plant_box(plant_box, now_utc_iso8601());
        const bool queued = ok ? command_dispatcher_.dispatch_current_config() : false;
        if (ok) {
            note_control_result("set_config", queued);
        }
        if (!ok) {
            body =
                "{\"ok\":false,\"queued\":false,\"error\":\"plantekasse kunne ikke gemmes (fx findes "
                "allerede en aktiv profil; kun én ad gangen).\"}\n";
        } else {
            body = std::string("{\"ok\":true,\"queued\":") + (queued ? "true" : "false") + "}\n";
        }
        return true;
    }
    if (request_line.rfind("POST /api/plant-boxes/delete", 0) == 0) {
        const auto query = parse_query(request_line);
        int id = 0;
        if (const auto it = query.find("id"); it != query.end()) {
            try {
                id = std::stoi(it->second);
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid id\"}\n";
                return true;
            }
        }
        if (id <= 0) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"id required\"}\n";
            return true;
        }
        const bool ok = db_manager_.delete_plant_box(id);
        if (!ok) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"unable to delete plant box (check id or minimum one box rule)\"}\n";
            return true;
        }
        const bool queued = command_dispatcher_.dispatch_current_config();
        note_control_result("set_config", queued);
        body = std::string("{\"ok\":true,\"queued\":") + (queued ? "true" : "false") + "}\n";
        return true;
    }
    status_line = "HTTP/1.1 405 Method Not Allowed\r\n";
    body = "{\"error\":\"method not allowed\"}\n";
    return true;
}

void WebUiServer::note_control_result(std::string action, bool queued) {
    const bool is_set_config = (action == "set_config");
    std::string profile_snap;
    if (is_set_config) {
        profile_snap = config_manager_.current_config().profile_name;
    }
    std::lock_guard<std::mutex> lock(status_mutex_);
    last_control_action_ = std::move(action);
    last_control_queued_ = queued;
    last_control_ts_ = now_utc_iso8601();
    if (is_set_config) {
        last_set_config_profiles_csv_ = std::move(profile_snap);
        last_set_config_queued_ok_ = queued;
        last_set_config_ts_ = last_control_ts_;
    }
}

bool WebUiServer::send_all(int client_socket, const std::string& payload) {
    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const int sent = send(client_socket, data, static_cast<int>(remaining), 0);
        if (sent <= 0) {
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

std::string WebUiServer::build_status_json() const {
    std::string last_hello;
    std::string last_control_action;
    std::string last_control_ts;
    bool last_control_queued;
    std::string last_set_csv;
    bool last_set_queued_ok;
    std::string last_set_ts;
    std::string last_field_live_components_json;
    std::string last_field_live_ts;
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        last_hello = last_hello_ts_;
        last_control_action = last_control_action_;
        last_control_ts = last_control_ts_;
        last_control_queued = last_control_queued_;
        last_set_csv = last_set_config_profiles_csv_;
        last_set_queued_ok = last_set_config_queued_ok_;
        last_set_ts = last_set_config_ts_;
        last_field_live_components_json = last_field_live_components_json_;
        last_field_live_ts = last_field_live_ts_;
    }
    const auto cfg = config_manager_.current_config();
    const bool profiles_match =
        profile_csv_sets_equal(cfg.profile_name, last_set_csv) && !last_set_csv.empty();
    const bool client_ok = tcp_server_.is_client_connected();
    const bool queue_matches_active = profiles_match && last_set_queued_ok && !last_set_csv.empty();
    const bool delivery_ok = queue_matches_active && client_ok;
    std::string delivery_hint;
    if (last_set_csv.empty()) {
        delivery_hint = "Ingen set_config endnu siden serverstart.";
    } else if (!profiles_match) {
        delivery_hint = "Aktiv profilvælgelse matcher ikke sidste set_config-kø.";
    } else if (!last_set_queued_ok) {
        delivery_hint = "Sidste set_config kom ikke i send-køen.";
    } else if (!client_ok) {
        delivery_hint = "Sidste set_config matcher gemt aktiv konfiguration; klient er frakoblet (besked ligger klar i køen).";
    } else {
        delivery_hint = "Profilvalg matcher sidste set_config i kø (hardware kvitterer ikke herfra).";
    }

    std::string field_node_body;
    if (last_field_live_ts == "never") {
        field_node_body =
            "\"telemetry_ts\": \"never\",\n"
            "    \"components\": null,\n"
            "    \"hint\": \"" +
            json_escape("Ingen Pi 2 telemetri med field_live endnu (eller ældre klient).") + "\"";
    } else {
        field_node_body =
            "\"telemetry_ts\": \"" + json_escape(last_field_live_ts) + "\",\n"
            "    \"components\": " +
            last_field_live_components_json;
    }

    return "{\n"
           "  \"service\": \"datahaandtering\",\n"
           "  \"ui\": \"running\",\n"
           "  \"client_connected\": "
           + std::string(client_ok ? "true" : "false")
           + ",\n"
           "  \"hello_count\": " + std::to_string(hello_count_.load()) + ",\n"
           "  \"last_hello_ts\": \"" + json_escape(last_hello) + "\",\n"
           "  \"last_control\": {\n"
           "    \"action\": \"" + json_escape(last_control_action) + "\",\n"
           "    \"queued\": " + std::string(last_control_queued ? "true" : "false") + ",\n"
           "    \"ts\": \"" + json_escape(last_control_ts) + "\"\n"
           "  },\n"
           "  \"last_set_config\": {\n"
           "    \"profile_name\": \"" + json_escape(last_set_csv) + "\",\n"
           "    \"queued_ok\": " + std::string(last_set_queued_ok ? "true" : "false") + ",\n"
           "    \"ts\": \"" + json_escape(last_set_ts) + "\"\n"
           "  },\n"
           "  \"config_queue_matches_active\": " + std::string(queue_matches_active ? "true" : "false") + ",\n"
           "  \"config_delivery_ok\": " + std::string(delivery_ok ? "true" : "false") + ",\n"
           "  \"config_delivery_hint\": \"" + json_escape(delivery_hint) + "\",\n"
           "  \"field_node\": {\n"
           "    " +
            field_node_body + "\n"
                              "  }\n"
                              "}\n";
}

std::string WebUiServer::build_config_json() const {
    const auto cfg = config_manager_.current_config();
    const auto profiles = dedupe_preserve_order(split_csv(cfg.profile_name));
    const std::string saved_at = db_manager_.read_active_config_timestamp_utc();
    std::ostringstream oss;
    oss << "{\n"
        << "  \"profile_name\": \"" << json_escape(cfg.profile_name) << "\",\n"
        << "  \"profile_names\": [";
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        oss << "\"" << json_escape(profiles[i]) << "\"";
        if (i + 1 < profiles.size()) {
            oss << ", ";
        }
    }
    oss << "],\n"
        << "  \"interval_sec\": " << cfg.interval_sec << ",\n"
        << "  \"emergency_stop\": " << (cfg.emergency_stop ? "true" : "false") << ",\n"
        << "  \"ventilation_enabled\": " << (cfg.ventilation_enabled ? "true" : "false") << ",\n"
        << "  \"watering_enabled\": " << (cfg.watering_enabled ? "true" : "false") << ",\n"
        << "  \"mode\": \"" << json_escape(to_string(cfg.mode)) << "\",\n"
        << "  \"saved_at\": \"" << json_escape(saved_at) << "\"\n"
        << "}\n";
    return oss.str();
}

std::string WebUiServer::build_profiles_json() const {
    const auto profiles = db_manager_.list_profiles();
    std::ostringstream oss;
    oss << "[\n";
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        const auto& p = profiles[i];
        oss << "  {\n"
            << "    \"name\": \"" << json_escape(p.name) << "\",\n"
            << "    \"min_humidity_air\": " << p.min_humidity_air << ",\n"
            << "    \"max_humidity_air\": " << p.max_humidity_air << ",\n"
            << "    \"min_humidity_soil\": " << p.min_humidity_soil << ",\n"
            << "    \"max_humidity_soil\": " << p.max_humidity_soil << ",\n"
            << "    \"min_temp_c\": " << p.min_temp_c << ",\n"
            << "    \"max_temp_c\": " << p.max_temp_c << ",\n"
            << "    \"interval_sec\": " << p.interval_sec << ",\n"
            << "    \"is_default\": " << (p.is_default ? "true" : "false") << "\n"
            << "  }";
        if (i + 1 < profiles.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "]\n";
    return oss.str();
}

std::string WebUiServer::build_plant_boxes_json() const {
    const auto plant_boxes = db_manager_.list_plant_boxes();
    std::ostringstream oss;
    oss << "[\n";
    for (std::size_t i = 0; i < plant_boxes.size(); ++i) {
        const auto& box = plant_boxes[i];
        oss << "  {\n"
            << "    \"id\": " << box.id << ",\n"
            << "    \"label\": \"" << json_escape(box.label) << "\",\n"
            << "    \"slot\": " << box.slot << ",\n"
            << "    \"is_active\": " << (box.is_active ? "true" : "false") << ",\n"
            << "    \"min_soil_pct\": "
            << (plant_box_min_soil_configured(box) ? std::to_string(box.min_soil_pct) : "null") << ",\n"
            << "    \"hysteresis_sec\": "
            << (plant_box_hysteresis_configured(box) ? std::to_string(box.hysteresis_sec) : "null") << "\n"
            << "  }";
        if (i + 1 < plant_boxes.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "]\n";
    return oss.str();
}

std::string WebUiServer::build_index_html() {
    return "<!doctype html>\n"
           "<html lang=\"da\">\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "  <title>Optihus</title>\n"
           "  <style>\n"
           "    :root { color-scheme: light; }\n"
           "    body { font-family: Inter, Segoe UI, Arial, sans-serif; background: #e4f5e4; margin: 0; color: #1f2937; }\n"
           "    .container { max-width: 1080px; margin: 0 auto; padding: 1.25rem; }\n"
           "    h1 { margin: 0 0 0.25rem 0; font-size: 1.9rem; }\n"
           "    h2 { margin: 0; font-size: 1.15rem; }\n"
           "    .muted { color: #6b7280; margin-top: 0.25rem; }\n"
           "    .grid { display: grid; gap: 1rem; grid-template-columns: repeat(auto-fit, minmax(320px, 1fr)); }\n"
           "    .card { background: #ffffff; border: 1px solid #d7ecd7; border-radius: 12px; padding: 1rem; box-shadow: 0 2px 6px rgba(16, 72, 16, 0.08); }\n"
           "    .row { display: flex; gap: 0.5rem; flex-wrap: wrap; align-items: end; }\n"
           "    .field { display: flex; flex-direction: column; gap: 0.2rem; min-width: 160px; }\n"
           "    .field > label { font-size: 0.82rem; color: #6b7280; }\n"
           "    input, select, button { font: inherit; }\n"
           "    input, select { border: 1px solid #d1d5db; border-radius: 8px; padding: 0.42rem 0.5rem; background: #fff; }\n"
           "    input.input-invalid { border-color: #dc2626 !important; box-shadow: 0 0 0 1px rgba(220, 38, 38, 0.28); }\n"
           "    button { border: 1px solid #15803d; border-radius: 8px; background: #16a34a; color: #fff; padding: 0.42rem 0.7rem; cursor: pointer; display: inline-flex; align-items: center; justify-content: center; text-align: center; }\n"
           "    button:disabled { opacity: 0.45; cursor: not-allowed; }\n"
           "    button.secondary { border-color: #eab308; background: #fefce8; color: #854d0e; }\n"
           "    button.btn-edit { border-color: #2563eb; background: #eff6ff; color: #1e3a8a; }\n"
           "    button.btn-edit:hover { background: #dbeafe; }\n"
           "    button.danger { border-color: #dc2626; background: #ef4444; }\n"
           "    .pill { display: inline-block; padding: 0.2rem 0.55rem; border-radius: 999px; font-size: 0.75rem; background: #e5e7eb; }\n"
           "    .status-tiles { display: grid; gap: 0.5rem; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); }\n"
           "    .status-tiles .tile { background: #f9fafb; border: 1px solid #e5e7eb; border-radius: 10px; padding: 0.5rem 0.6rem; }\n"
           "    .status-tiles .tile.tile-emergency { background: #fef2f2; border-color: #fecaca; box-shadow: 0 0 0 1px rgba(220, 38, 38, 0.22); }\n"
           "    .status-tiles .tile.tile-emergency .val { color: #991b1b; }\n"
           "    .status-tiles .tile .lab { font-size: 0.7rem; color: #6b7280; text-transform: uppercase; letter-spacing: 0.04em; }\n"
           "    .status-tiles .tile .val { font-weight: 600; margin-top: 0.15rem; word-break: break-word; font-size: 0.88rem; }\n"
           "    .pill.ok { background: #dcfce7; color: #166534; }\n"
           "    .pill.warn { background: #fef3c7; color: #92400e; }\n"
           "    .pill.server { background: #dcfce7; color: #14532d; border: 1px solid #86efac; }\n"
           "    .pill.draft { background: #fef9c3; color: #713f12; border: 1px solid #fde047; }\n"
           "    .pill.sent { background: #dbeafe; color: #1e3a8a; border: 1px solid #93c5fd; }\n"
           "    .pill.pending { background: #ffedd5; color: #7c2d12; border: 1px solid #fdba74; }\n"
           "    .box { background: #f9fafb; border: 1px solid #e5e7eb; border-radius: 10px; padding: 0.65rem; }\n"
           "    .box-active { border: 2px solid #22c55e; background: linear-gradient(180deg, #f0fdf4 0%, #fafafa 60%); box-shadow: 0 0 0 1px rgba(34,197,94,0.12); }\n"
           "    .box-draft { border: 2px solid #eab308; background: linear-gradient(180deg, #fffbeb 0%, #fafafa 60%); box-shadow: 0 0 0 1px rgba(234,179,8,0.12); }\n"
           "    .kv { margin: 0.2rem 0; font-size: 0.88rem; }\n"
           "    .kv strong { color: #374151; font-weight: 600; }\n"
           "    .section-head { display: flex; justify-content: space-between; align-items: flex-start; gap: 1rem; flex-wrap: wrap; margin-bottom: 0.15rem; }\n"
           "    .section-tag { font-size: 0.7rem; letter-spacing: 0.08em; text-transform: uppercase; color: #166534; font-weight: 700; background: #ecfdf5; border: 1px solid #bbf7d0; border-radius: 6px; padding: 0.2rem 0.5rem; }\n"
           "    .status-summary { font-size: 0.88rem; color: #374151; margin: 0.55rem 0 0.2rem 0; line-height: 1.45; }\n"
           "    .status-tech { margin-top: 0.5rem; }\n"
           "    .status-tech .status-tech-label { font-size: 0.82rem; color: #4b5563; font-weight: 600; margin-bottom: 0.35rem; }\n"
           "    .status-tech pre { margin-top: 0.25rem; }\n"
           "    .hint { font-size: 0.85rem; color: #6b7280; margin-top: 0.35rem; }\n"
           "    .warn { color: #92400e; }\n"
           "    .drift-help-row { grid-column: 1 / -1; display: grid; grid-template-columns: 2fr 1fr; gap: 1rem; align-items: stretch; }\n"
           "    .drift-help-row > .card { min-width: 0; }\n"
           "    @media (max-width: 960px) {\n"
           "      .drift-help-row { grid-template-columns: 1fr; }\n"
           "    }\n"
           "    .drift-subtitle { margin: 0 0 0.2rem 0; font-size: 1rem; font-weight: 700; color: #111827; }\n"
           "    .drift-lead { margin: 0 0 0.75rem 0; font-size: 0.82rem; color: #6b7280; line-height: 1.45; max-width: 52rem; }\n"
           "    .drift-status { padding-bottom: 1rem; margin-bottom: 1rem; border-bottom: 1px solid #e5e7eb; }\n"
           "    .drift-actions { background: transparent; border: none; border-radius: 0; padding: 0.5rem 0 0 0; box-shadow: none; }\n"
           "    .drift-actions-critical { display: flex; flex-wrap: wrap; gap: 0.5rem; align-items: center; padding: 0.65rem 0.75rem; margin: 0 0 0.75rem 0; background: #f9fafb; border: 1px solid #d1d5db; border-radius: 10px; }\n"
           "    .drift-actions-critical .critical-label { font-size: 0.78rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em; color: #991b1b; width: 100%; margin: 0 0 0.15rem 0; }\n"
           "    .drift-addon-box { margin-top: 0.75rem; padding: 0.65rem 0.75rem; background: #f9fafb; border: 1px solid #d1d5db; border-radius: 10px; }\n"
           "    .drift-addon-title { margin: 0 0 0.4rem 0; font-size: 0.82rem; font-weight: 700; color: #4b5563; text-transform: uppercase; letter-spacing: 0.04em; }\n"
           "    details.debug-details { margin-top: 1rem; padding: 0.65rem 0.75rem 0.65rem 0.85rem; background: #f9fafb; border: 1px solid #e5e7eb; border-radius: 10px; }\n"
           "    details.debug-details summary { cursor: pointer; font-weight: 600; font-size: 0.88rem; color: #374151; user-select: none; list-style-position: inside; }\n"
           "    details.debug-details[open] summary { margin-bottom: 0.35rem; }\n"
           "    details.debug-details .debug-json-block + .debug-json-block { margin-top: 1rem; padding-top: 1rem; border-top: 1px dashed #d1d5db; }\n"
           "    .info-panel { margin-top: 0.75rem; background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 10px; padding: 0.7rem; }\n"
           "    .info-panel h3 { margin: 0 0 0.35rem 0; font-size: 0.98rem; color: #166534; }\n"
           "    .info-panel ul { margin: 0; padding-left: 1rem; color: #334155; font-size: 0.82rem; line-height: 1.35; }\n"
           "    .info-panel li { margin: 0.2rem 0; }\n"
           "    pre { background: #f3f4f6; padding: 0.75rem; border-radius: 8px; overflow: auto; font-size: 0.84rem; }\n"
           "    table { width: 100%; border-collapse: collapse; font-size: 0.9rem; }\n"
           "    th, td { text-align: left; padding: 0.45rem; border-bottom: 1px solid #e5e7eb; }\n"
           "    table.data-table { width: 100%; }\n"
           "    table.data-table thead th {\n"
           "      font-size: 0.82rem;\n"
           "      font-weight: 600;\n"
           "      color: #374151;\n"
           "      background: #f3f4f6;\n"
           "      border-bottom: 1px solid #d1d5db;\n"
           "      padding: 0.48rem 0.55rem;\n"
           "      vertical-align: middle;\n"
           "      line-height: 1.25;\n"
           "    }\n"
           "    table.data-table tbody td {\n"
           "      padding: 0.4rem 0.55rem;\n"
           "      vertical-align: middle;\n"
           "      line-height: 1.35;\n"
           "      font-size: 0.88rem;\n"
           "    }\n"
           "    table.data-table tbody td:first-child { overflow-wrap: anywhere; word-break: break-word; }\n"
           "    table.data-table td[colspan] { text-align: center; color: #6b7280; padding: 0.65rem 0.5rem; }\n"
           "    table.data-table th:last-child { text-align: right; white-space: nowrap; vertical-align: middle; }\n"
           "    table.data-table td:last-child {\n"
           "      text-align: right;\n"
           "      vertical-align: middle;\n"
           "      white-space: nowrap;\n"
           "    }\n"
           "    table.data-table td:last-child button { vertical-align: middle; }\n"
           "    table.data-table td:last-child button:not(:first-child) { margin-left: 0.45rem; }\n"
           "    .profile-form-row { display: flex; flex-wrap: wrap; align-items: flex-end; gap: 0.5rem; }\n"
           "    .profile-form-row > .field.profile-field-crop { flex: 0 1 200px; min-width: 140px; }\n"
           "    .profile-mid-cluster {\n"
           "      display: flex; flex-wrap: wrap; align-items: flex-end; gap: 0.5rem;\n"
           "      flex: 1 1 260px; justify-content: center; padding: 0 0.35rem; min-width: 0;\n"
           "    }\n"
           "    .profile-mid-cluster .field { min-width: 118px; }\n"
           "    .profile-form-actions { display: flex; flex-wrap: wrap; align-items: flex-end; gap: 0.5rem; flex: 0 0 auto; margin-left: auto; }\n"
           "    table.data-table.profiles-data { table-layout: fixed; width: 100%; }\n"
           "    table.data-table.profiles-data th:nth-child(1),\n"
           "    table.data-table.profiles-data td:nth-child(1) { width: 30%; vertical-align: middle; }\n"
           "    table.data-table.profiles-data th:nth-child(2),\n"
           "    table.data-table.profiles-data td:nth-child(2),\n"
           "    table.data-table.profiles-data th:nth-child(3),\n"
           "    table.data-table.profiles-data td:nth-child(3) {\n"
           "      width: 22%;\n"
           "      text-align: center;\n"
           "      vertical-align: middle;\n"
           "    }\n"
           "    table.data-table.profiles-data th:nth-child(4),\n"
           "    table.data-table.profiles-data td:nth-child(4) { width: 26%; vertical-align: middle; }\n"
           "    table.data-table.plant-boxes-data { table-layout: auto; width: 100%; font-size: 0.82rem; }\n"
           "    table.data-table.plant-boxes-data td:last-child,\n"
           "    table.data-table.plant-boxes-data th:last-child { white-space: nowrap; }\n"
           "    table.data-table tbody tr:last-child td { border-bottom: none; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <div class=\"container\">\n"
           "    <h1>Optihus Kontrolpanel</h1>\n"
           "    <p class=\"muted\">Profilvalg og plantekasser redigeres i sektionerne nedenfor og bliver først aktivt efter gemte ændringer er sendt.</p>\n"
           "    <div class=\"grid\">\n"
           "      <div class=\"drift-help-row\">\n"
           "      <section class=\"card drift-card\">\n"
           "        <h2>Drift</h2>\n"
           "        <p class=\"muted\" style=\"margin-top:0.25rem;\">Status (læsning) og handlinger (konsekvens) er adskilt, så nøgletal i status ikke forveksles med nødstop m.m.</p>\n"
           "        <div class=\"drift-status\">\n"
           "          <h3 class=\"drift-subtitle\">Status</h3>\n"
           "          <p class=\"drift-lead\">Overvågning og kontekst: hvordan går det lige nu?</p>\n"
           "          <!-- Key Performance Indicators: connection, client hello, last control, last set_config -->\n"
           "          <div class=\"status-tiles\" id=\"statusTiles\"></div>\n"
           "          <p class=\"status-summary\" id=\"statusHuman\">Indlæser...</p>\n"
           "        </div>\n"
           "        <div class=\"drift-actions\">\n"
           "          <h3 class=\"drift-subtitle\">Handlinger</h3>\n"
           "          <p class=\"drift-lead\">Nødstop, ventilation og vanding gemmes og sendes med det samme til Pi 2. Grafana nedenfor er kun read-only overblik.</p>\n"
           "          <div class=\"drift-actions-critical\">\n"
           "            <span class=\"critical-label\">Nødstop</span>\n"
           "            <button type=\"button\" onclick=\"sendControl('/api/control/emergency-stop?enabled=1')\" class=\"danger\">Nødstop</button>\n"
           "            <button type=\"button\" onclick=\"sendControl('/api/control/emergency-stop?enabled=0')\" class=\"secondary\">Fortsæt drift</button>\n"
           "          </div>\n"
           "          <div class=\"drift-actions-critical\" style=\"margin-top:0.65rem;\">\n"
           "            <span class=\"critical-label\">Ventilation</span>\n"
           "            <button type=\"button\" id=\"btnVentOn\" class=\"secondary\" onclick=\"setSystemEnabled('ventilation', true)\">Til</button>\n"
           "            <button type=\"button\" id=\"btnVentOff\" class=\"secondary\" onclick=\"setSystemEnabled('ventilation', false)\">Fra</button>\n"
           "            <span class=\"muted\" id=\"ventToggleState\" style=\"margin-left:0.35rem;font-size:0.85rem;\">—</span>\n"
           "          </div>\n"
           "          <div class=\"drift-actions-critical\" style=\"margin-top:0.45rem;\">\n"
           "            <span class=\"critical-label\">Vanding</span>\n"
           "            <button type=\"button\" id=\"btnWaterOn\" class=\"secondary\" onclick=\"setSystemEnabled('watering', true)\">Til</button>\n"
           "            <button type=\"button\" id=\"btnWaterOff\" class=\"secondary\" onclick=\"setSystemEnabled('watering', false)\">Fra</button>\n"
           "            <span class=\"muted\" id=\"waterToggleState\" style=\"margin-left:0.35rem;font-size:0.85rem;\">—</span>\n"
           "          </div>\n"
           "          <div class=\"drift-addon-box\">\n"
           "            <h4 class=\"drift-addon-title\">Grafana</h4>\n"
           "            <button type=\"button\" onclick=\"openGrafana()\" class=\"secondary\">Åbn Grafana (read-only)</button>\n"
           "            <p class=\"hint\" style=\"margin-top:0.45rem;margin-bottom:0;\">Åbner Grafana i et nyt vindue som read-only overblik (ikke styring af anlægget herfra).</p>\n"
           "          </div>\n"
           "        </div>\n"
           "        <details class=\"debug-details\">\n"
           "          <summary>Fejlsøgning: teknisk JSON (skjult som udgangspunkt)</summary>\n"
           "          <p class=\"hint\" style=\"margin-top:0;\">Primært til integrationstest og implementeringsfejlsøgning. To <strong>forskellige kilder</strong>. Status opdateres løbende fra serveren; styringssvar viser kun sidste POST. Under status‑JSON findes <code>field_node</code>: seneste live‑oversigt fra Pi&nbsp;2 (TCP‑link, sensor‑loops, om aktuation er tilladt); opdateres fra feltnodens telemetri‑felt <code>field_live</code>.</p>\n"
           "          <div class=\"status-tech debug-json-block\">\n"
           "            <div class=\"status-tech-label\">Status: rå JSON fra <code>GET /api/status</code></div>\n"
           "            <pre id=\"statusRaw\">Indlæser...</pre>\n"
           "          </div>\n"
           "          <div class=\"status-tech debug-json-block\">\n"
           "            <div class=\"status-tech-label\">Seneste styringssvar: rå JSON fra sidste <code>POST</code> til styring</div>\n"
           "            <pre id=\"control\">Ingen handling endnu.</pre>\n"
           "          </div>\n"
           "        </details>\n"
           "      </section>\n"
           "      <section class=\"card\">\n"
           "        <h2>Forklaring af indstillinger</h2>\n"
           "        <div class=\"info-panel\">\n"
           "          <h3>Vigtigste begreber</h3>\n"
           "          <ul>\n"
           "            <li><strong>Aktiv konfiguration (server):</strong> det der er gemt i databasen; i oversigten herunder vises valgte profiler og tidspunkt for seneste gem. Nødstop og øvrig drift håndteres under <em>Drift</em>.</li>\n"
           "            <li><strong>Kladde:</strong> dit valg i tabellen før du trykker <em>Gem konfiguration</em>. Afviger kladde fra det gemte, bør du gemme eller justere valget.</li>\n"
           "            <li><strong>set_config:</strong> besked til miljøstyring med grænser m.m. Sendes ved <em>Gem konfiguration</em> (kladde gemmes og sendes, hvis klient/kø tillader det) eller via <em>Send konfiguration</em> under <em>Aktiv konfiguration</em> (kun gensend den allerede gemte aktive konfiguration).</li>\n"
           "            <li><strong>Afgrøde-type / profil:</strong> ét navn du vælger; det er også den unikke nøgle. <em>Redigér</em> fylder formularen; nyt navn erstatter den gamle profil og opdaterer aktiv konfiguration i databasen hvis navnet blev brugt der.</li>\n"
           "            <li><strong>To slags profiler:</strong> <strong>Afgrøde-profiler</strong> (sektionen <em>Profiler</em>) gemmer luft- og jordfugt-grænser pr. navn. Du vælger hvilke der skal med under <em>Gem konfiguration</em>; de sendes i <code>set_config</code> og bruges til luftgrænser for vinduet (og til motor og temperaturfelter). Jordfelterne i den samme besked (<code>bounds</code>) styrer <strong>ikke</strong> vanding i v1. <strong>Plantekasseprofiler</strong> (sektionen <em>Plantekasser</em>): du udfylder selv <strong>minimum jordfugtighed (%)</strong> og <strong>pause mellem vandinger (sek.)</strong> — ingen systemstandard. Pi 2 bruger kun den aktive profil når begge felter er sat og præcis én plantekasse er aktiv; pumpe kører <strong>1&nbsp;s</strong> pr. gang (~<strong>25&nbsp;ml</strong> ved ca. 25&nbsp;ml/s — planlæg efter kassestørrelse og din pumpe).</li>\n"
           "            <li><strong>Mærkat <span class=\"pill server\">Gemt</span>:</strong> profilen indgår i den <em>gemte</em> aktive konfiguration på serveren (ikke det samme som at Pi har kvitteret).</li>\n"
           "            <li><strong>Mærkat <span class=\"pill draft\">Kladde</span>:</strong> profilen er valgt til næste <em>Gem konfiguration</em> men indgår <em>ikke</em> i den <em>gemte</em> aktive konfiguration endnu; vises ikke hvis profilen allerede er gemt som del af aktiv konfiguration.</li>\n"
           "            <li><strong>Send til klient / kø:</strong> om sidste <code>set_config</code> matcher den gemte konfiguration og om en klient er tilkoblet, fremgår under <em>Drift</em>, overskriften <em>Status</em> (samme for hele systemet, ikke per profil-række).</li>\n"
           "            <li><strong>Luft- og jordfugtighed i profiler:</strong> nedre/øvre grænser; ved flere valgte profiler i aktiv konfiguration beregnes fælles grænser (højeste minimum, laveste maksimum) og sendes i <code>bounds</code> til miljøstyring til <strong>luft</strong> m.m. De samme jordfelter i <code>bounds</code> styrer <strong>ikke</strong> vanding på Pi 2 i v1 (se <strong>Plantekasser</strong> nedenfor).</li>\n"
           "            <li><strong>Plantekasser (<code>plant_boxes</code>):</strong> valgfrit. Kun <strong>fuldt udfyldte</strong> profiler gemmes og sendes til Pi. <strong>Højst én</strong> aktiv ad gangen. <em>Vanding til/fra</em> under <em>Drift</em> styrer om pumpen må køre; mængde vand er fast (~25&nbsp;ml pr. 1&nbsp;s i feltet).</li>\n"
           "            <li><strong>Telemetri:</strong> fast interval 60 s til serveren.</li>\n"
           "            <li><strong>Nødstop:</strong> kun via knapper under <em>Drift</em>, overskriften <em>Handlinger</em>.</li>\n"
           "          </ul>\n"
           "        </div>\n"
           "      </section>\n"
           "      </div>\n"
           "      <section class=\"card card-official\" style=\"grid-column: 1 / -1;\">\n"
           "        <div class=\"section-head\">\n"
           "          <h2>Aktiv konfiguration</h2>\n"
           "          <span class=\"section-tag\">Server</span>\n"
           "        </div>\n"
           "        <p class=\"hint\" style=\"margin-top:0;\">Her vises den <strong>gemte</strong> aktive konfiguration i databasen og din <strong>kladde</strong> før du gemmer. Grøn/gul ramme skelner servertilstand fra kladde.</p>\n"
           "        <div class=\"row\" style=\"margin-top:0.55rem;\">\n"
           "          <button type=\"button\" id=\"btnSaveConfig\" disabled onclick=\"saveConfig()\">Gem konfiguration</button>\n"
           "          <button type=\"button\" onclick=\"sendControl('/api/control/sync-config')\">Send konfiguration</button>\n"
           "        </div>\n"
           "        <p class=\"hint\"><strong>Send konfiguration</strong> lægger den <em>gemte</em> aktive konfiguration i send-køen igen uden at gemme kladde først (fx hvis klienten missede sidste besked).</p>\n"
           "        <p class=\"hint\">Ved flere profiler beregnes fælles grænser (højeste minimum, laveste maksimum). Overlapper profilerne ikke, afvises gem. Telemetri-interval er fast 60 sekunder.</p>\n"
           "        <div class=\"config-compare\" style=\"display:grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 0.75rem; margin-top: 0.65rem;\">\n"
           "          <div class=\"field\" style=\"min-width:0; flex:1;\"><label>Gemt aktiv konfiguration</label><div id=\"activeSummary\" class=\"box box-active\">Indlæser...</div></div>\n"
           "          <div class=\"field\" style=\"min-width:0; flex:1;\"><label>Kladde (ikke gemt)</label><div id=\"draftSummary\" class=\"box box-draft\">Indlæser...</div></div>\n"
           "        </div>\n"
           "        <div class=\"hint warn\" id=\"profileRequirementMsg\"></div>\n"
           "      </section>\n"
           "      <section class=\"card\" style=\"grid-column: 1 / -1;\">\n"
           "        <h2>Profiler</h2>\n"
           "        <div class=\"row profile-form-row\">\n"
           "          <div class=\"field profile-field-crop\"><label>Afgrøde-type</label><input id=\"p_crop\" autocomplete=\"off\" placeholder=\"f.eks. tomat\"></div>\n"
           "          <div class=\"profile-mid-cluster\">\n"
           "            <div class=\"field\"><label>Minimum luftfugtighed</label><input id=\"p_min_air\" type=\"number\" min=\"0\" max=\"99\" step=\"0.1\" inputmode=\"decimal\" placeholder=\"f.eks. 50\" autocomplete=\"off\"></div>\n"
           "            <div class=\"field\"><label>Maksimum luftfugtighed</label><input id=\"p_max_air\" type=\"number\" min=\"0\" max=\"99\" step=\"0.1\" inputmode=\"decimal\" placeholder=\"f.eks. 70\" autocomplete=\"off\"></div>\n"
           "            <div class=\"field\"><label>Minimum jordfugtighed</label><input id=\"p_min_soil\" type=\"number\" min=\"0\" max=\"99\" step=\"0.1\" inputmode=\"decimal\" placeholder=\"f.eks. 30\" autocomplete=\"off\"></div>\n"
           "            <div class=\"field\"><label>Maksimum jordfugtighed</label><input id=\"p_max_soil\" type=\"number\" min=\"0\" max=\"99\" step=\"0.1\" inputmode=\"decimal\" placeholder=\"f.eks. 55\" autocomplete=\"off\"></div>\n"
           "            <div class=\"field\"><label>Minimum temperatur (°C)</label><input id=\"p_min_temp\" type=\"number\" min=\"-10\" max=\"50\" step=\"0.1\" inputmode=\"decimal\" placeholder=\"f.eks. 18\" autocomplete=\"off\"></div>\n"
           "            <div class=\"field\"><label>Maksimum temperatur (°C)</label><input id=\"p_max_temp\" type=\"number\" min=\"-10\" max=\"50\" step=\"0.1\" inputmode=\"decimal\" placeholder=\"f.eks. 26\" autocomplete=\"off\"></div>\n"
           "          </div>\n"
           "          <div class=\"profile-form-actions\">\n"
           "            <button type=\"button\" id=\"btnProfileSave\" disabled onclick=\"saveProfile()\">Gem profil</button>\n"
           "            <button type=\"button\" id=\"btnProfileCancelEdit\" class=\"secondary\" style=\"display:none\" onclick=\"cancelProfileEdit()\">Annullér redigering</button>\n"
           "          </div>\n"
           "        </div>\n"
           "        <p class=\"hint\" style=\"margin-top:0.35rem;\">Luft- og jordfugtighed: <strong>procent (0 til 99)</strong>. Temperatur (vindue/motor): <strong>grader Celsius (-10 til 50)</strong> under felterne ovenfor — gem profil og derefter <strong>Gem konfiguration</strong> for at sende til Pi 2. Højst ét ciffer efter decimal (komma eller punktum).</p>\n"
           "        <p class=\"hint warn\" id=\"profileDuplicateHint\" style=\"display:none;margin-top:0.25rem;\"></p>\n"
           "        <p class=\"hint\" id=\"profileEditHint\"></p>\n"
           "        <div style=\"overflow:auto; margin-top:0.8rem;\">\n"
           "          <table class=\"data-table profiles-data\">\n"
           "            <thead><tr><th>Afgrøde-type</th><th>Luft min/maks</th><th>Jord min/maks</th><th>Temp min/maks (C)</th><th>Handlinger</th></tr></thead>\n"
           "            <tbody id=\"profilesBody\"><tr><td colspan=\"5\">Indlæser...</td></tr></tbody>\n"
           "          </table>\n"
           "        </div>\n"
           "        <p class=\"hint\"><span class=\"pill server\">Gemt</span> = profil i <em>gemt</em> aktiv konfiguration på serveren. <span class=\"pill draft\">Kladde</span> = valgt til næste <strong>Gem konfiguration</strong> men <em>ikke</em> i den gemte aktive konfiguration endnu. Om sidste <code>set_config</code> matcher og om klient er tilkoblet, står under <em>Drift</em>, overskriften <em>Status</em>. Ændrede profil-tal kræver <strong>Gem konfiguration</strong> for næste send.</p>\n"
           "      </section>\n"
           "      <section class=\"card\" style=\"grid-column: 1 / -1;\">\n"
           "        <h2>Plantekasser</h2>\n"
           "        <p class=\"hint\">Her opretter du <strong>plantekasseprofiler</strong> (gemmes som <code>plant_boxes</code> og sendes i <code>set_config</code>). Pi 2 bruger i dag kun slot 1 fysisk; øvrige rækker er til senere multi-kasse.</p>\n"
           "        <p class=\"hint warn\"><strong>Plantekasse (valgfri):</strong> Ingen aktiv profil er OK. Vanding kræver <em>Vanding til</em> under Drift, præcis én aktiv profil, udfyldte tal herunder, og at nødstop ikke er aktiv. Pumpe i feltet: <strong>1&nbsp;s tændt</strong> ≈ <strong>25&nbsp;ml</strong> (ca. 25&nbsp;ml/s) — juster <em>pause</em> og <em>minimum jordfugt</em> efter kasse og pumpe; der er ingen forudfyldt standard.</p>\n"
           "        <div class=\"row\">\n"
           "          <div class=\"field\"><label>Id (tom = ny)</label><input id=\"pb_id\" type=\"number\" min=\"1\" placeholder=\"auto\"></div>\n"
           "          <div class=\"field\"><label>Navn</label><input id=\"pb_label\" placeholder=\"Plantekasse 1\"></div>\n"
           "          <div class=\"field\"><label>Pladsnr.</label><input id=\"pb_slot\" type=\"number\" min=\"1\" value=\"1\"></div>\n"
           "          <div class=\"field\"><label>Aktiv</label><select id=\"pb_active\"><option value=\"0\">nej</option><option value=\"1\">ja</option></select></div>\n"
           "        </div>\n"
           "        <div class=\"row\" style=\"margin-top:0.45rem;\">\n"
           "          <div class=\"field\"><label>Minimum jordfugtighed (%)</label><input id=\"pb_min_soil\" type=\"number\" min=\"0\" max=\"100\" step=\"1\" placeholder=\"f.eks. 35 — du vælger\"></div>\n"
           "          <div class=\"field\"><label>Pause mellem vandinger (sek.)</label><input id=\"pb_hysteresis\" type=\"number\" min=\"0\" max=\"86400\" step=\"1\" placeholder=\"f.eks. 600 — du vælger\"></div>\n"
           "          <button type=\"button\" onclick=\"savePlantBox()\">Gem plantekasse</button>\n"
           "        </div>\n"
           "        <div style=\"overflow:auto; margin-top:0.8rem;\">\n"
           "          <table class=\"data-table plant-boxes-data\">\n"
           "            <thead><tr><th>Id</th><th>Navn</th><th>Pladsnr.</th><th>Jord min (%)</th><th>Pause (s)</th><th>Aktiv</th><th>Handlinger</th></tr></thead>\n"
           "            <tbody id=\"plantBoxesBody\"><tr><td colspan=\"7\">Indlæser...</td></tr></tbody>\n"
           "          </table>\n"
           "        </div>\n"
           "        <p class=\"hint\">Under <em>Aktiv</em>: <span class=\"pill ok\">ja</span> / <span class=\"pill warn\">nej</span> — højst én aktiv ad gangen. <strong>Ingen aktiv</strong> er tilladt (valgfri plantekasse). Brug <em>Vanding til/fra</em> under Drift for at slå pumpen fra uanset plantekasse.</p>\n"
           "      </section>\n"
           "    </div>\n"
           "  </div>\n"
           "  <script>\n"
           "    let latestProfiles = [];\n"
           "    let latestPlantBoxes = [];\n"
           "    let activeConfig = null;\n"
           "    let latestServiceStatus = null;\n"
           "    const draftConfig = { profiles: [] };\n"
           "    let draftDirty = false;\n"
           "    let profileEditFrom = null;\n"
           "    const profileFormFieldIds = ['p_crop', 'p_min_air', 'p_max_air', 'p_min_soil', 'p_max_soil', 'p_min_temp', 'p_max_temp'];\n"
           "    function normalizeProfileNumber(raw) {\n"
           "      return String(raw ?? '').trim().replace(',', '.');\n"
           "    }\n"
           "    function profileNumericInputOk(raw) {\n"
           "      const t = normalizeProfileNumber(raw);\n"
           "      if (!t) return false;\n"
           "      if (!/^\\d+(\\.\\d)?$/.test(t)) return false;\n"
           "      const n = Number(t);\n"
           "      if (!Number.isFinite(n) || n < 0 || n > 99) return false;\n"
           "      return true;\n"
           "    }\n"
           "    function profileTempInputOk(raw) {\n"
           "      const t = normalizeProfileNumber(raw);\n"
           "      if (!t) return false;\n"
           "      if (!/^-?\\d+(\\.\\d)?$/.test(t)) return false;\n"
           "      const n = Number(t);\n"
           "      if (!Number.isFinite(n) || n < -10 || n > 50) return false;\n"
           "      return true;\n"
           "    }\n"
           "    function profileTempForApi(raw) {\n"
           "      const n = Number(normalizeProfileNumber(raw));\n"
           "      if (!Number.isFinite(n)) return '';\n"
           "      const clamped = Math.min(50, Math.max(-10, n));\n"
           "      const r = Math.round(clamped * 10) / 10;\n"
           "      if (Object.is(r, -0)) return '0';\n"
           "      if (Math.abs(r - Math.round(r)) < 1e-9) return String(Math.round(r));\n"
           "      return (Math.round(r * 10) / 10).toFixed(1);\n"
           "    }\n"
           "    /** Luft/jord %: 0-99, max et decimaltegn; afrundes til 1 decimal ved gem. */\n"
           "    function profileHumidityForApi(raw) {\n"
           "      const n = Number(normalizeProfileNumber(raw));\n"
           "      if (!Number.isFinite(n)) return '';\n"
           "      const clamped = Math.min(99, Math.max(0, n));\n"
           "      const r = Math.round(clamped * 10) / 10;\n"
           "      if (Object.is(r, -0)) return '0';\n"
           "      if (Math.abs(r - Math.round(r)) < 1e-9) return String(Math.round(r));\n"
           "      return (Math.round(r * 10) / 10).toFixed(1);\n"
           "    }\n"
           "    function updateProfileFormValidation() {\n"
           "      let formOk = true;\n"
           "      for (const id of profileFormFieldIds) {\n"
           "        const el = document.getElementById(id);\n"
           "        if (!el) continue;\n"
           "        const ok = id === 'p_crop' ? String(el.value ?? '').trim().length > 0 : (id === 'p_min_temp' || id === 'p_max_temp') ? profileTempInputOk(el.value) : profileNumericInputOk(el.value);\n"
           "        if (!ok) {\n"
           "          formOk = false;\n"
           "          el.classList.add('input-invalid');\n"
           "        } else {\n"
           "          el.classList.remove('input-invalid');\n"
           "        }\n"
           "      }\n"
           "      const cropEl = document.getElementById('p_crop');\n"
           "      const crop = cropEl ? String(cropEl.value ?? '').trim() : '';\n"
           "      let nameTaken = false;\n"
           "      if (crop && Array.isArray(latestProfiles)) {\n"
           "        nameTaken = profileEditFrom\n"
           "          ? latestProfiles.some(p => p.name === crop && p.name !== profileEditFrom)\n"
           "          : latestProfiles.some(p => p.name === crop);\n"
           "      }\n"
           "      const dupHint = document.getElementById('profileDuplicateHint');\n"
           "      if (nameTaken) {\n"
           "        formOk = false;\n"
           "        if (cropEl) cropEl.classList.add('input-invalid');\n"
           "        if (dupHint) {\n"
           "          dupHint.textContent = profileEditFrom\n"
           "            ? `Navnet \"${crop}\" bruges allerede af en anden profil. Vælg et andet navn.`\n"
           "            : `Profilen \"${crop}\" findes allerede. Vælg et andet navn, eller tryk Redigér på den eksisterende række for at ændre grænserne.`;\n"
           "          dupHint.style.display = 'block';\n"
           "        }\n"
           "      } else if (dupHint) {\n"
           "        dupHint.textContent = '';\n"
           "        dupHint.style.display = 'none';\n"
           "      }\n"
           "      const btn = document.getElementById('btnProfileSave');\n"
           "      if (btn) btn.disabled = !formOk;\n"
           "      return formOk;\n"
           "    }\n"
           "    function updateProfileCancelVisibility() {\n"
           "      const b = document.getElementById('btnProfileCancelEdit');\n"
           "      if (b) b.style.display = profileEditFrom ? 'inline-flex' : 'none';\n"
           "    }\n"
           "    function initProfileFormListeners() {\n"
           "      for (const id of profileFormFieldIds) {\n"
           "        const el = document.getElementById(id);\n"
           "        if (!el) continue;\n"
           "        el.addEventListener('input', () => { updateProfileFormValidation(); });\n"
           "        el.addEventListener('change', () => { updateProfileFormValidation(); });\n"
           "      }\n"
           "      updateProfileFormValidation();\n"
           "      updateProfileCancelVisibility();\n"
           "    }\n"
           "    function escapeHtml(value) {\n"
           "      return String(value ?? '').replace(/[&<>\\\"']/g, ch => ({'&':'&amp;','<':'&lt;','>':'&gt;','\\\"':'&quot;',\"'\":'&#39;'}[ch]));\n"
           "    }\n"
           "    function statusUiText(raw) {\n"
           "      const s = String(raw ?? '').trim();\n"
           "      if (s === 'never') return 'aldrig';\n"
           "      if (s === 'none') return 'ingen';\n"
           "      return s;\n"
           "    }\n"
           "    function dedupe(list) {\n"
           "      const out = [];\n"
           "      const seen = new Set();\n"
           "      for (const item of list) {\n"
           "        if (!item || seen.has(item)) continue;\n"
           "        seen.add(item);\n"
           "        out.push(item);\n"
           "      }\n"
           "      return out;\n"
           "    }\n"
           "    function parseCsvProfiles(value) {\n"
           "      if (!value) return [];\n"
           "      return dedupe(value.split(',').map(v => v.trim()).filter(v => v.length > 0));\n"
           "    }\n"
           "    function profilesEqual(a, b) {\n"
           "      if (a.length !== b.length) return false;\n"
           "      for (let i = 0; i < a.length; i += 1) {\n"
           "        if (a[i] !== b[i]) return false;\n"
           "      }\n"
           "      return true;\n"
           "    }\n"
           "    function profileSetsEqual(a, b) {\n"
           "      if (a.length !== b.length) return false;\n"
           "      const sa = [...a].sort();\n"
           "      const sb = [...b].sort();\n"
           "      for (let i = 0; i < sa.length; i += 1) {\n"
           "        if (sa[i] !== sb[i]) return false;\n"
           "      }\n"
           "      return true;\n"
           "    }\n"
           "    function activeProfiles() {\n"
           "      if (!activeConfig) return [];\n"
           "      if (Array.isArray(activeConfig.profile_names)) return dedupe(activeConfig.profile_names);\n"
           "      return parseCsvProfiles(activeConfig.profile_name || '');\n"
           "    }\n"
           "    function aggregateProfileBounds(profileNames) {\n"
           "      if (!Array.isArray(latestProfiles) || !profileNames || profileNames.length === 0) return null;\n"
           "      const selected = profileNames.map(n => latestProfiles.find(p => p.name === n)).filter(Boolean);\n"
           "      if (selected.length === 0) return null;\n"
           "      let b = {\n"
           "        min_air: selected[0].min_humidity_air,\n"
           "        max_air: selected[0].max_humidity_air,\n"
           "        min_soil: selected[0].min_humidity_soil,\n"
           "        max_soil: selected[0].max_humidity_soil,\n"
           "        min_temp: selected[0].min_temp_c,\n"
           "        max_temp: selected[0].max_temp_c,\n"
           "      };\n"
           "      for (let i = 1; i < selected.length; i += 1) {\n"
           "        const p = selected[i];\n"
           "        b.min_air = Math.max(b.min_air, p.min_humidity_air);\n"
           "        b.max_air = Math.min(b.max_air, p.max_humidity_air);\n"
           "        b.min_soil = Math.max(b.min_soil, p.min_humidity_soil);\n"
           "        b.max_soil = Math.min(b.max_soil, p.max_humidity_soil);\n"
           "        b.min_temp = Math.max(b.min_temp, p.min_temp_c);\n"
           "        b.max_temp = Math.min(b.max_temp, p.max_temp_c);\n"
           "      }\n"
           "      if (b.min_air > b.max_air || b.min_soil > b.max_soil || b.min_temp > b.max_temp) return null;\n"
           "      return b;\n"
           "    }\n"
           "    function formatBoundsLine(b) {\n"
           "      if (!b) return '<div class=\"kv muted\">Grænser: overlapper ikke mellem valgte profiler (juster tal eller vælg færre).</div>';\n"
           "      return `<div class=\"kv\"><strong>Luft:</strong> ${escapeHtml(b.min_air)}–${escapeHtml(b.max_air)} % &nbsp; <strong>Jord (profil):</strong> ${escapeHtml(b.min_soil)}–${escapeHtml(b.max_soil)} % &nbsp; <strong>Temp:</strong> ${escapeHtml(b.min_temp)}–${escapeHtml(b.max_temp)} °C</div>`;\n"
           "    }\n"
           "    function syncDraftFromActive() {\n"
           "      if (!activeConfig) return;\n"
           "      draftConfig.profiles = activeProfiles();\n"
           "    }\n"
           "    function renderConfigViews() {\n"
           "      const active = activeProfiles();\n"
           "      const activeSummary = document.getElementById('activeSummary');\n"
           "      const draftSummary = document.getElementById('draftSummary');\n"
           "      const requirement = document.getElementById('profileRequirementMsg');\n"
           "      const saved = activeConfig && activeConfig.saved_at ? escapeHtml(String(activeConfig.saved_at)) : '-';\n"
           "      const del = latestServiceStatus || {};\n"
           "      const sendLine = del.config_delivery_hint ? `<div class=\"muted\" style=\"margin-top:0.45rem;font-size:0.82rem;line-height:1.35;\">${escapeHtml(del.config_delivery_hint)}</div>` : '';\n"
           "      activeSummary.innerHTML = `\n"
           "        <div><div class=\"kv\"><strong>Profiler:</strong> ${escapeHtml(active.join(', ') || 'ingen')}</div>\n"
           "        ${formatBoundsLine(aggregateProfileBounds(active))}\n"
           "        <div class=\"kv\"><strong>Ventilation:</strong> ${activeConfig && activeConfig.ventilation_enabled !== false ? 'til' : 'fra'} &nbsp; <strong>Vanding:</strong> ${activeConfig && activeConfig.watering_enabled !== false ? 'til' : 'fra'}</div>\n"
           "        <div class=\"kv\"><strong>Senest gemt (server):</strong> ${saved}</div>\n"
           "        ${sendLine}`;\n"
           "      const draft = draftConfig.profiles;\n"
           "      const draftDiffers = !profileSetsEqual(active, draft);\n"
           "      const draftTag = draftDiffers\n"
           "        ? '<span class=\"pill pending\" style=\"margin-left:0.35rem;\">Afviger fra gemt</span>'\n"
           "        : '<span class=\"pill ok\" style=\"margin-left:0.35rem;\">Matcher gemt</span>';\n"
           "      draftSummary.innerHTML = `\n"
           "        <div class=\"kv\"><strong>Profiler:</strong> ${escapeHtml(draft.join(', ') || 'ingen')}${draftTag}</div>\n"
           "        ${formatBoundsLine(aggregateProfileBounds(draft))}\n"
           "        <div class=\"muted\" style=\"margin-top:0.45rem;font-size:0.82rem;\">Tryk <strong>Gem konfiguration</strong> for at skrive kladde til databasen og sætte <code>set_config</code> i kø (hvis klient er tilkoblet).</div>`;\n"
           "      if (draftConfig.profiles.length === 0) {\n"
           "        requirement.textContent = 'Du skal vælge mindst én profil i Profiler-sektionen før konfigurationen kan gemmes og køre.';\n"
           "      } else {\n"
           "        requirement.textContent = '';\n"
           "      }\n"
           "      const btnSave = document.getElementById('btnSaveConfig');\n"
           "      if (btnSave) {\n"
           "        btnSave.disabled = draftConfig.profiles.length === 0 || !draftDiffers;\n"
           "      }\n"
           "      renderProfilesTable();\n"
           "    }\n"
           "    function toggleProfile(encodedName) {\n"
           "      const name = decodeURIComponent(encodedName);\n"
           "      const exists = draftConfig.profiles.includes(name);\n"
           "      if (exists) {\n"
           "        draftConfig.profiles = draftConfig.profiles.filter(p => p !== name);\n"
           "      } else {\n"
           "        draftConfig.profiles = dedupe([...draftConfig.profiles, name]);\n"
           "      }\n"
           "      draftDirty = true;\n"
           "      renderConfigViews();\n"
           "    }\n"
           "    function renderProfilesTable() {\n"
           "      const body = document.getElementById('profilesBody');\n"
           "      if (!Array.isArray(latestProfiles) || latestProfiles.length === 0) {\n"
           "        body.innerHTML = '<tr><td colspan=\"5\">Ingen profiler</td></tr>';\n"
           "        return;\n"
           "      }\n"
           "      const active = activeProfiles();\n"
           "      body.innerHTML = latestProfiles.map(p => {\n"
           "        const chosen = draftConfig.profiles.includes(p.name);\n"
           "        const toggleLabel = chosen ? 'Fjern' : 'Vælg';\n"
           "        const toggleClass = chosen ? 'secondary' : '';\n"
           "        const inActive = active.includes(p.name);\n"
           "        const badges = `${inActive ? '<span class=\\\"pill server\\\">Gemt</span> ' : ''}${chosen && !inActive ? '<span class=\\\"pill draft\\\">Kladde</span> ' : ''}`;\n"
           "        return `\n"
           "          <tr>\n"
           "            <td>${escapeHtml(p.name)}${badges ? ' ' + badges : ''}</td>\n"
           "            <td>${escapeHtml(p.min_humidity_air)} / ${escapeHtml(p.max_humidity_air)}</td>\n"
           "            <td>${escapeHtml(p.min_humidity_soil)} / ${escapeHtml(p.max_humidity_soil)}</td>\n"
           "            <td>${escapeHtml(p.min_temp_c ?? '')} / ${escapeHtml(p.max_temp_c ?? '')}</td>\n"
           "            <td>\n"
           "              <button class=\"${toggleClass}\" onclick=\"toggleProfile('${encodeURIComponent(p.name)}')\">${toggleLabel}</button>\n"
           "              <button class=\"btn-edit\" onclick=\"editProfile('${encodeURIComponent(p.name)}')\">Redigér</button>\n"
           "              <button class=\"danger\" onclick=\"deleteProfile('${encodeURIComponent(p.name)}')\" ${p.is_default ? 'disabled title=\"Standardprofiler kan ikke slettes\" style=\"opacity:0.5;cursor:not-allowed\"' : ''}>Slet</button>\n"
           "            </td>\n"
           "          </tr>`;\n"
           "      }).join('');\n"
           "    }\n"
           "    function editProfile(encodedName) {\n"
           "      const name = decodeURIComponent(encodedName);\n"
           "      const p = latestProfiles.find(x => x.name === name);\n"
           "      if (!p) return;\n"
           "      profileEditFrom = name;\n"
           "      document.getElementById('p_crop').value = String(p.name || '');\n"
           "      document.getElementById('p_min_air').value = String(p.min_humidity_air ?? '');\n"
           "      document.getElementById('p_max_air').value = String(p.max_humidity_air ?? '');\n"
           "      document.getElementById('p_min_soil').value = String(p.min_humidity_soil ?? '');\n"
           "      document.getElementById('p_max_soil').value = String(p.max_humidity_soil ?? '');\n"
           "      document.getElementById('p_min_temp').value = String(p.min_temp_c ?? '');\n"
           "      document.getElementById('p_max_temp').value = String(p.max_temp_c ?? '');\n"
           "      document.getElementById('profileEditHint').textContent = 'Redigerer denne profil. Samme navn = opdater tal. Nyt navn = den gamle profil slettes og erstattes; aktiv konfiguration opdateres automatisk hvis den brugte det gamle navn.';\n"
           "      updateProfileCancelVisibility();\n"
           "      updateProfileFormValidation();\n"
           "    }\n"
           "    function cancelProfileEdit() {\n"
           "      profileEditFrom = null;\n"
           "      document.getElementById('p_crop').value = '';\n"
           "      document.getElementById('p_min_air').value = '';\n"
           "      document.getElementById('p_max_air').value = '';\n"
           "      document.getElementById('p_min_soil').value = '';\n"
           "      document.getElementById('p_max_soil').value = '';\n"
           "      document.getElementById('p_min_temp').value = '';\n"
           "      document.getElementById('p_max_temp').value = '';\n"
           "      document.getElementById('profileEditHint').textContent = '';\n"
           "      updateProfileCancelVisibility();\n"
           "      updateProfileFormValidation();\n"
           "    }\n"
           "    function editPlantBox(encodedId) {\n"
           "      const id = Number(decodeURIComponent(encodedId));\n"
           "      const current = latestPlantBoxes.find(pb => Number(pb.id) === id);\n"
           "      if (!current) return;\n"
           "      document.getElementById('pb_id').value = String(current.id);\n"
           "      document.getElementById('pb_label').value = String(current.label || '');\n"
           "      document.getElementById('pb_slot').value = String(current.slot || 1);\n"
           "      document.getElementById('pb_active').value = current.is_active ? '1' : '0';\n"
           "      const minSoil = current.min_soil_pct;\n"
           "      document.getElementById('pb_min_soil').value =\n"
           "        (minSoil != null && minSoil >= 0) ? String(minSoil) : '';\n"
           "      const hyst = current.hysteresis_sec;\n"
           "      document.getElementById('pb_hysteresis').value =\n"
           "        (hyst != null && hyst >= 0) ? String(hyst) : '';\n"
           "    }\n"
           "    function renderPlantBoxesTable() {\n"
           "      const body = document.getElementById('plantBoxesBody');\n"
           "      if (!Array.isArray(latestPlantBoxes) || latestPlantBoxes.length === 0) {\n"
           "        body.innerHTML = '<tr><td colspan=\"7\">Ingen plantekasser</td></tr>';\n"
           "        return;\n"
           "      }\n"
           "      body.innerHTML = latestPlantBoxes.map(pb => `\n"
           "        <tr>\n"
           "          <td>${escapeHtml(pb.id)}</td>\n"
           "          <td>${escapeHtml(pb.label)}</td>\n"
           "          <td>${escapeHtml(pb.slot)}</td>\n"
           "          <td>${pb.min_soil_pct != null && pb.min_soil_pct >= 0 ? escapeHtml(pb.min_soil_pct) : '—'}</td>\n"
           "          <td>${pb.hysteresis_sec != null && pb.hysteresis_sec >= 0 ? escapeHtml(pb.hysteresis_sec) : '—'}</td>\n"
           "          <td>${pb.is_active ? '<span class=\\\"pill ok\\\">ja</span>' : '<span class=\\\"pill warn\\\">nej</span>'}</td>\n"
           "          <td>\n"
           "            <button class=\"btn-edit\" onclick=\"editPlantBox('${encodeURIComponent(String(pb.id))}')\">Redigér</button>\n"
           "            <button class=\"danger\" onclick=\"deletePlantBox('${encodeURIComponent(String(pb.id))}')\">Slet</button>\n"
           "          </td>\n"
           "        </tr>\n"
           "      `).join('');\n"
           "    }\n"
           "    async function loadStatus() {\n"
           "      try {\n"
           "        const res = await fetch('/api/status');\n"
           "        const json = await res.json();\n"
           "        latestServiceStatus = json;\n"
           "        document.getElementById('statusRaw').textContent = JSON.stringify(json, null, 2);\n"
           "        const connected = json.client_connected;\n"
           "        const lsc = json.last_set_config || {};\n"
           "        document.getElementById('statusHuman').textContent = json.config_delivery_hint || '';\n"
           "        document.getElementById('statusTiles').innerHTML = `\n"
           "          <div class=\"tile\"><div class=\"lab\">Forbindelse</div><div class=\"val\"><span class=\"pill ${connected ? 'ok' : 'warn'}\">${connected ? 'Tilkoblet' : 'Frakoblet'}</span></div></div>\n"
           "          <div class=\"tile\"><div class=\"lab\">Klient (hello)</div><div class=\"val\">${escapeHtml(String(json.hello_count))}, seneste: ${escapeHtml(statusUiText(json.last_hello_ts ?? 'never'))}</div></div>\n"
           "          <div class=\"tile${(json.last_control && json.last_control.action === 'emergency_stop') ? ' tile-emergency' : ''}\"><div class=\"lab\">Seneste styring</div><div class=\"val\">${escapeHtml(statusUiText(json.last_control?.action ?? 'none'))}</div></div>\n"
           "          <div class=\"tile\"><div class=\"lab\">Sidste set_config</div><div class=\"val\">${escapeHtml(lsc.profile_name ? String(lsc.profile_name) : '-')}<br><span class=\"muted\" style=\"font-size:0.8rem;font-weight:500;\">Send-kø OK:</span> ${lsc.queued_ok === true ? 'ja' : 'nej'}</div></div>\n"
           "        `;\n"
           "        renderConfigViews();\n"
           "      } catch (e) {\n"
           "        latestServiceStatus = null;\n"
           "        document.getElementById('statusHuman').textContent = 'Status utilgængelig: ' + e;\n"
           "        document.getElementById('statusRaw').textContent = '';\n"
           "        document.getElementById('statusTiles').innerHTML = '';\n"
           "        renderConfigViews();\n"
           "      }\n"
           "    }\n"
           "    function updateSystemToggleUi() {\n"
           "      const ventOn = activeConfig && activeConfig.ventilation_enabled !== false;\n"
           "      const waterOn = activeConfig && activeConfig.watering_enabled !== false;\n"
           "      const ventEl = document.getElementById('ventToggleState');\n"
           "      const waterEl = document.getElementById('waterToggleState');\n"
           "      if (ventEl) ventEl.textContent = ventOn ? 'til (gemt)' : 'fra (gemt)';\n"
           "      if (waterEl) waterEl.textContent = waterOn ? 'til (gemt)' : 'fra (gemt)';\n"
           "      const styleOn = (on) => on ? 'font-weight:600' : 'opacity:0.55';\n"
           "      const bvo = document.getElementById('btnVentOn');\n"
           "      const bvf = document.getElementById('btnVentOff');\n"
           "      const bwo = document.getElementById('btnWaterOn');\n"
           "      const bwf = document.getElementById('btnWaterOff');\n"
           "      if (bvo) bvo.style.cssText = styleOn(ventOn);\n"
           "      if (bvf) bvf.style.cssText = styleOn(!ventOn);\n"
           "      if (bwo) bwo.style.cssText = styleOn(waterOn);\n"
           "      if (bwf) bwf.style.cssText = styleOn(!waterOn);\n"
           "    }\n"
           "    async function setSystemEnabled(which, enabled) {\n"
           "      const key = which === 'ventilation' ? 'ventilation_enabled' : 'watering_enabled';\n"
           "      const val = enabled ? '1' : '0';\n"
           "      const result = await sendControl(`/api/config?${key}=${val}`);\n"
           "      if (result?.ok) {\n"
           "        await loadConfig();\n"
           "      } else if (result?.error) {\n"
           "        alert(result.error);\n"
           "      }\n"
           "    }\n"
           "    async function loadConfig() {\n"
           "      try {\n"
           "        const res = await fetch('/api/config');\n"
           "        activeConfig = await res.json();\n"
           "        if (!draftDirty) {\n"
           "          syncDraftFromActive();\n"
           "        }\n"
           "        updateSystemToggleUi();\n"
           "        renderConfigViews();\n"
           "      } catch (e) {\n"
           "        const err = 'Konfiguration utilgængelig: ' + e;\n"
           "        document.getElementById('activeSummary').innerHTML = '<div class=\"kv\">' + err + '</div>';\n"
           "        document.getElementById('draftSummary').innerHTML = '<div class=\"kv\">' + err + '</div>';\n"
           "      }\n"
           "    }\n"
           "    async function loadProfiles() {\n"
           "      try {\n"
           "        const res = await fetch('/api/profiles');\n"
           "        latestProfiles = await res.json();\n"
           "        if (!Array.isArray(latestProfiles)) latestProfiles = [];\n"
           "        const available = new Set(latestProfiles.map(p => p.name));\n"
           "        draftConfig.profiles = draftConfig.profiles.filter(p => available.has(p));\n"
           "        renderConfigViews();\n"
           "        updateProfileFormValidation();\n"
           "      } catch (e) {\n"
           "        document.getElementById('profilesBody').innerHTML = '<tr><td colspan=\"5\">Profiler utilgængelige: ' + e + '</td></tr>';\n"
           "      }\n"
           "    }\n"
           "    async function loadPlantBoxes() {\n"
           "      try {\n"
           "        const res = await fetch('/api/plant-boxes');\n"
           "        latestPlantBoxes = await res.json();\n"
           "        if (!Array.isArray(latestPlantBoxes)) latestPlantBoxes = [];\n"
           "        renderPlantBoxesTable();\n"
           "      } catch (e) {\n"
           "        document.getElementById('plantBoxesBody').innerHTML = '<tr><td colspan=\"9\">Plantekasser utilgængelige: ' + e + '</td></tr>';\n"
           "      }\n"
           "    }\n"
           "    async function deleteProfile(encodedName) {\n"
           "      const profile = decodeURIComponent(encodedName);\n"
           "      if (!confirm(`Slet profil ${profile}? Dette fjerner profilen permanent.`)) return;\n"
           "      const result = await sendControl(`/api/profiles/delete?name=${encodedName}`);\n"
           "      if (result?.ok) {\n"
           "        alert(`Profilen ${profile} er slettet.`);\n"
           "      } else if (result?.error) {\n"
           "        alert(`Kunne ikke slette profil: ${result.error}`);\n"
           "      }\n"
           "      draftConfig.profiles = draftConfig.profiles.filter(p => p !== profile);\n"
           "      await loadProfiles();\n"
           "      await loadConfig();\n"
           "    }\n"
           "    async function saveConfig() {\n"
           "      if (draftConfig.profiles.length === 0) {\n"
           "        alert('Du skal vælge mindst én profil før du kan gemme konfigurationen.');\n"
           "        return;\n"
           "      }\n"
           "      const profilesParam = encodeURIComponent(draftConfig.profiles.join(','));\n"
           "      const result = await sendControl(`/api/config?profiles=${profilesParam}`);\n"
           "      if (result?.ok) {\n"
           "        draftDirty = false;\n"
           "      }\n"
           "      await loadConfig();\n"
           "    }\n"
           "    async function saveProfile() {\n"
           "      if (!updateProfileFormValidation()) return;\n"
           "      const cropRaw = document.getElementById('p_crop').value.trim();\n"
           "      const name = encodeURIComponent(cropRaw);\n"
           "      const min_air = encodeURIComponent(profileHumidityForApi(document.getElementById('p_min_air').value));\n"
           "      const max_air = encodeURIComponent(profileHumidityForApi(document.getElementById('p_max_air').value));\n"
           "      const min_soil = encodeURIComponent(profileHumidityForApi(document.getElementById('p_min_soil').value));\n"
           "      const max_soil = encodeURIComponent(profileHumidityForApi(document.getElementById('p_max_soil').value));\n"
           "      const min_temp = encodeURIComponent(profileTempForApi(document.getElementById('p_min_temp').value));\n"
           "      const max_temp = encodeURIComponent(profileTempForApi(document.getElementById('p_max_temp').value));\n"
           "      let url = `/api/profiles/upsert?name=${name}&min_air=${min_air}&max_air=${max_air}&min_soil=${min_soil}&max_soil=${max_soil}&min_temp=${min_temp}&max_temp=${max_temp}&interval=60`;\n"
           "      if (profileEditFrom) {\n"
           "        url += `&replace_from=${encodeURIComponent(profileEditFrom)}`;\n"
           "      }\n"
           "      const result = await sendControl(url);\n"
           "      if (!result) return;\n"
           "      if (result.http_ok === false) {\n"
           "        alert(result.error ? `Kunne ikke gemme profil: ${result.error}` : 'Kunne ikke gemme profil.');\n"
           "        return;\n"
           "      }\n"
           "      if (profileEditFrom && profileEditFrom !== cropRaw && result.ok) {\n"
           "        draftConfig.profiles = draftConfig.profiles.map(n => (n === profileEditFrom ? cropRaw : n));\n"
           "        draftDirty = true;\n"
           "      }\n"
           "      profileEditFrom = null;\n"
           "      if (result.replaced_profile) {\n"
           "        document.getElementById('profileEditHint').textContent = 'Profilen er omdøbt, og aktiv konfiguration er opdateret hvor det gamle navn blev brugt. Tryk Gem konfiguration for at sende grænser til klienten.';\n"
           "      } else {\n"
           "        document.getElementById('profileEditHint').textContent = '';\n"
           "      }\n"
           "      await loadProfiles();\n"
           "      await loadConfig();\n"
           "      updateProfileCancelVisibility();\n"
           "      updateProfileFormValidation();\n"
           "    }\n"
           "    async function savePlantBox() {\n"
           "      const idRaw = document.getElementById('pb_id').value.trim();\n"
           "      const editId = idRaw && Number.isFinite(Number(idRaw)) ? Number(idRaw) : 0;\n"
           "      const label = document.getElementById('pb_label').value.trim();\n"
           "      const slot = document.getElementById('pb_slot').value.trim();\n"
           "      const isActive = document.getElementById('pb_active').value === '1' ? '1' : '0';\n"
           "      if (!label) {\n"
           "        alert('Navn er påkrævet for plantekasse.');\n"
           "        return;\n"
           "      }\n"
           "      if (isActive === '1' && Array.isArray(latestPlantBoxes)) {\n"
           "        const other = latestPlantBoxes.find(pb => pb.is_active && Number(pb.id) !== editId);\n"
           "        if (other) {\n"
           "          const who = other.label ? `\"${String(other.label)}\"` : `id ${other.id}`;\n"
           "          alert(`Kun én aktiv plantekasseprofil ad gangen. Deaktivér først ${who}, eller gem som inaktiv.`);\n"
           "          return;\n"
           "        }\n"
           "      }\n"
           "      const parts = [];\n"
           "      if (idRaw) parts.push(`id=${encodeURIComponent(idRaw)}`);\n"
           "      parts.push(`label=${encodeURIComponent(label)}`);\n"
           "      parts.push(`slot=${encodeURIComponent(slot || '1')}`);\n"
           "      parts.push(`is_active=${encodeURIComponent(isActive)}`);\n"
           "      const minSoilRaw = document.getElementById('pb_min_soil').value.trim();\n"
           "      const hystRaw = document.getElementById('pb_hysteresis').value.trim();\n"
           "      if (!minSoilRaw || !hystRaw) {\n"
           "        alert('Udfyld minimum jordfugtighed (%) og pause mellem vandinger (sek.). Ufuldstændige profiler gemmes ikke og sendes ikke til Pi. Vandmængde: fast ~25 ml pr. vanding (1 s i feltet) — planlæg pause og tør-grænse efter kasse og pumpe.');\n"
           "        return;\n"
           "      }\n"
           "      parts.push(`min_soil_pct=${encodeURIComponent(minSoilRaw)}`);\n"
           "      parts.push(`hysteresis_sec=${encodeURIComponent(hystRaw)}`);\n"
           "      const result = await sendControl(`/api/plant-boxes/upsert?${parts.join('&')}`);\n"
           "      if (!result) {\n"
           "        return;\n"
           "      }\n"
           "      if (result.ok === false) {\n"
           "        alert(result.error || 'Plantekassen kunne ikke gemmes.');\n"
           "        await loadPlantBoxes();\n"
           "        return;\n"
           "      }\n"
           "      document.getElementById('pb_id').value = '';\n"
           "      document.getElementById('pb_label').value = '';\n"
           "      document.getElementById('pb_slot').value = '1';\n"
           "      document.getElementById('pb_active').value = '0';\n"
           "      document.getElementById('pb_min_soil').value = '';\n"
           "      document.getElementById('pb_hysteresis').value = '';\n"
           "      await loadPlantBoxes();\n"
           "    }\n"
           "    async function deletePlantBox(encodedId) {\n"
           "      const id = decodeURIComponent(encodedId);\n"
           "      if (!confirm(`Slet plantekasse med id ${id}?`)) return;\n"
           "      const result = await sendControl(`/api/plant-boxes/delete?id=${encodeURIComponent(id)}`);\n"
           "      if (result?.error) {\n"
           "        alert(`Kunne ikke slette plantekasse: ${result.error}`);\n"
           "      }\n"
           "      await loadPlantBoxes();\n"
           "    }\n"
           "    async function sendControl(url) {\n"
           "      const target = document.getElementById('control');\n"
           "      target.textContent = 'Sender...';\n"
           "      try {\n"
           "        const res = await fetch(url, { method: 'POST' });\n"
           "        const json = await res.json();\n"
           "        json.http_ok = res.ok;\n"
           "        target.textContent = JSON.stringify(json, null, 2);\n"
           "        await loadStatus();\n"
           "        return json;\n"
           "      } catch (e) {\n"
           "        target.textContent = 'Handling fejlede: ' + e;\n"
           "        return null;\n"
           "      }\n"
           "    }\n"
           "    function openGrafana() {\n"
           "      const grafanaUrl = 'http://192.168.4.1:3000/d/ad99gg7/optihus-read-only-overview?orgId=1&from=now-24h&to=now&timezone=browser';\n"
           "      window.open(grafanaUrl, '_blank', 'noopener,noreferrer');\n"
           "    }\n"
           "    initProfileFormListeners();\n"
           "    loadStatus();\n"
           "    loadConfig();\n"
           "    loadProfiles();\n"
           "    loadPlantBoxes();\n"
           "    setInterval(loadStatus, 2000);\n"
           "    setInterval(loadConfig, 5000);\n"
           "    setInterval(loadProfiles, 10000);\n"
           "    setInterval(loadPlantBoxes, 10000);\n"
           "  </script>\n"
           "</body>\n"
           "</html>\n";
}

