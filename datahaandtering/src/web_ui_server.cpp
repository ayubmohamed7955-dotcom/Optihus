
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

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "shared/logger.hpp"
#include "shared/types.hpp"
#include "shared/time_utils.hpp"

static std::string last_socket_error() {
    return std::strerror(errno);
}

static void close_socket(int socket_fd) {
    close(socket_fd);
}

#include "datahaandtering/web_ui_html.hpp"
#include "datahaandtering/web_ui_http_util.hpp"
#include "datahaandtering/web_ui_debug.hpp"



static std::set<std::string> profile_name_set(const std::vector<Profile>& profiles) {
    std::set<std::string> names;
    for (const auto& p : profiles) {
        names.insert(p.name);
    }
    return names;
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

    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ < 0) {
        log_error("web_ui", "Failed to create socket: " + last_socket_error());
        running_ = false;
        return false;
    }

    int reuse = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        log_error("web_ui", "bind failed: " + last_socket_error());
        close_socket(listen_socket_);
        listen_socket_ = -1;
        running_ = false;
        return false;
    }

    if (listen(listen_socket_, 8) < 0) {
        log_error("web_ui", "listen failed: " + last_socket_error());
        close_socket(listen_socket_);
        listen_socket_ = -1;
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
        shutdown(listen_socket_, SHUT_RDWR);
        close_socket(listen_socket_);
        listen_socket_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

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
        || request_line.rfind("POST /api/plant-boxes/set-active", 0) == 0
        || request_line.rfind("POST /api/plant-boxes/delete", 0) == 0) {
        handle_plant_boxes_request(request_line, status_line, body, content_type);
    } else if (datahaandtering::web_ui_debug::is_debug_control_path(request_line)) {
        content_type = "application/json; charset=utf-8";
        if (!datahaandtering::web_ui_debug::handle_test_component_request(
                request_line,
                config_manager_,
                command_dispatcher_,
                [this](std::string action, bool queued) { note_control_result(std::move(action), queued); },
                status_line,
                body)) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid debug request\"}\n";
        }
    } else if (request_line.rfind("POST /api/control/", 0) == 0) {
        if (!handle_control_request(request_line, status_line, body, content_type)) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid control request\"}\n";
        }
    } else if (request_line.rfind("GET / ", 0) == 0) {
        body = build_web_index_html();
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

    if (request_line.rfind("POST /api/config/factory-reset", 0) == 0) {
        if (!db_manager_.apply_factory_defaults()) {
            status_line = "HTTP/1.1 500 Internal Server Error\r\n";
            body = "{\"ok\":false,\"error\":\"factory reset failed\"}\n";
            return true;
        }
        if (!config_manager_.load_active_config()) {
            status_line = "HTTP/1.1 500 Internal Server Error\r\n";
            body = "{\"ok\":false,\"error\":\"could not reload active config\"}\n";
            return true;
        }
        const bool queued = command_dispatcher_.dispatch_current_config();
        note_control_result("set_config", queued);
        body = std::string("{\"ok\":true,\"action\":\"factory_reset\",\"set_config_queued\":")
            + (queued ? "true" : "false")
            + "}\n";
        return true;
    }

    if (request_line.rfind("POST /api/config", 0) == 0) {
        const auto query = web_ui_parse_query(request_line);
        bool changed = false;
        if (const auto it = query.find("profiles"); it != query.end()) {
            const auto selected = web_ui_dedupe_preserve_order(web_ui_split_csv(it->second));
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
                    body = "{\"error\":\"unknown profile: " + web_ui_json_escape(name) + "\"}\n";
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
            WebUiAggregatedBounds bounds;
            if (!web_ui_aggregate_bounds_if_compatible(selected_profiles, bounds)) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"selected profiles are not compatible (no overlap in humidity ranges)\"}\n";
                return true;
            }
            config_manager_.set_profile_name(web_ui_join_csv(selected));
            changed = true;
        } else if (const auto it = query.find("profile"); it != query.end() && !it->second.empty()) {
            const auto available_profiles = db_manager_.list_profiles();
            const auto available_names = profile_name_set(available_profiles);
            if (available_names.find(it->second) == available_names.end()) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"unknown profile: " + web_ui_json_escape(it->second) + "\"}\n";
                return true;
            }
            config_manager_.set_profile_name(it->second);
            changed = true;
        }
        if (const auto vent = web_ui_parse_bool_query(query, "ventilation_enabled"); vent.has_value()) {
            config_manager_.set_ventilation_enabled(*vent);
            changed = true;
        }
        if (const auto water = web_ui_parse_bool_query(query, "watering_enabled"); water.has_value()) {
            config_manager_.set_watering_enabled(*water);
            changed = true;
        }
        if (changed) {
            config_manager_.set_interval_sec(60);
            const bool saved = config_manager_.save_active_config();
            body = std::string("{\"ok\":")
                + (saved ? "true" : "false")
                + ",\"saved\":"
                + (saved ? "true" : "false")
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
        const auto query = web_ui_parse_query(request_line);
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

        std::string replace_from;
        if (const auto it = query.find("replace_from"); it != query.end()) {
            replace_from = it->second;
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
            if (p.min_humidity_air > p.max_humidity_air) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"min_air must be <= max_air\"}\n";
                return true;
            }
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
                if (old_p->is_default) {
                    status_line = "HTTP/1.1 403 Forbidden\r\n";
                    body = "{\"error\":\"default profile names cannot be changed\"}\n";
                    return true;
                }
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
                web_ui_migrate_profile_name_in_active_config(config_manager_, replace_from, p.name);
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
        const auto query = web_ui_parse_query(request_line);
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
            auto selected = web_ui_dedupe_preserve_order(web_ui_split_csv(config_manager_.current_config().profile_name));
            selected.erase(
                std::remove(selected.begin(), selected.end(), it->second),
                selected.end()
            );
            if (!selected.empty()) {
                config_manager_.set_profile_name(web_ui_join_csv(selected));
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
        const auto query = web_ui_parse_query(request_line);
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
        bool have_slot = false;
        if (const auto it = query.find("slot"); it != query.end() && !it->second.empty()) {
            try {
                plant_box.slot = std::stoi(it->second);
                have_slot = true;
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid slot\"}\n";
                return true;
            }
        }
        if (const auto it = query.find("is_active"); it != query.end()) {
            plant_box.is_active = (it->second == "1" || it->second == "true");
        } else if (plant_box.id <= 0) {
            plant_box.is_active = false;
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

        if (plant_box.label.empty()) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"label required\"}\n";
            return true;
        }

        const auto all_boxes = db_manager_.list_plant_boxes();
        std::optional<PlantBoxConfig> existing;
        if (plant_box.id > 0) {
            for (const auto& b : all_boxes) {
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
            plant_box.is_default = existing->is_default;
            plant_box.is_active = existing->is_active;
            plant_box.slot = have_slot ? plant_box.slot : existing->slot;
        } else {
            if (have_min_soil) {
                plant_box.min_soil_pct = min_soil_val;
            }
            if (have_hyst) {
                plant_box.hysteresis_sec = hyst_val;
            }
            plant_box.is_default = false;
            if (!have_slot) {
                int next_slot = 1;
                for (const auto& b : all_boxes) {
                    next_slot = std::max(next_slot, b.slot + 1);
                }
                plant_box.slot = next_slot;
            }
        }

        if (plant_box.slot < 1) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid slot\"}\n";
            return true;
        }

        if (!plant_box_watering_params_configured(plant_box)) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body =
                "{\"error\":\"Udfyld minimum JordFugt (%) og JordPause (sek.) for plantekassen. "
                "Ufuldstaendige profiler gemmes ikke og sendes ikke til Pi. Pumpe: fast 1 s (~25 ml) i feltet.\"}\n";
            return true;
        }
        if (plant_box.min_soil_pct < 0.0 || plant_box.min_soil_pct > 100.0) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid min_soil_pct (expect 0..100)\"}\n";
            return true;
        }
        if (plant_box.hysteresis_sec < 10 || plant_box.hysteresis_sec > 86400) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid hysteresis_sec (expect 10..86400)\"}\n";
            return true;
        }

        const bool ok = db_manager_.upsert_plant_box(plant_box, now_utc_iso8601());
        if (!ok) {
            body =
                "{\"ok\":false,\"error\":\"plantekasse kunne ikke gemmes (fx findes "
                "allerede en aktiv profil; kun en ad gangen).\"}\n";
        } else {
            body = "{\"ok\":true}\n";
        }
        return true;
    }
    if (request_line.rfind("POST /api/plant-boxes/set-active", 0) == 0) {
        const auto query = web_ui_parse_query(request_line);
        int id = 0;
        bool active = false;
        if (const auto it = query.find("id"); it != query.end()) {
            try {
                id = std::stoi(it->second);
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid id\"}\n";
                return true;
            }
        }
        if (const auto it = query.find("active"); it != query.end()) {
            active = (it->second == "1" || it->second == "true");
        }
        if (id <= 0) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"id required\"}\n";
            return true;
        }
        const bool ok = db_manager_.set_plant_box_active(id, active, now_utc_iso8601());
        if (!ok) {
            body =
                "{\"ok\":false,\"error\":\"Kunne ikke aendre aktiv plantekasse (fx findes "
                "allerede en anden aktiv profil).\"}\n";
        } else {
            body = "{\"ok\":true}\n";
        }
        return true;
    }
    if (request_line.rfind("POST /api/plant-boxes/delete", 0) == 0) {
        const auto query = web_ui_parse_query(request_line);
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
        for (const auto& box : db_manager_.list_plant_boxes()) {
            if (box.id == id && box.is_default) {
                status_line = "HTTP/1.1 403 Forbidden\r\n";
                body = "{\"error\":\"plantekasse kan ikke slettes\"}\n";
                return true;
            }
        }
        const bool ok = db_manager_.delete_plant_box(id);
        if (!ok) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"unable to delete plant box (check id or minimum one box rule)\"}\n";
            return true;
        }
        body = "{\"ok\":true}\n";
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
        web_ui_profile_csv_sets_equal(cfg.profile_name, last_set_csv) && !last_set_csv.empty();
    const bool client_ok = tcp_server_.is_client_connected();
    const bool queue_matches_active = profiles_match && last_set_queued_ok && !last_set_csv.empty();
    const bool delivery_ok = queue_matches_active && client_ok;
    const bool field_node_has_telemetry = !last_field_live_ts.empty() && last_field_live_ts != "never";
    const bool field_node_up_to_date_for_last_set =
        !last_set_ts.empty() && field_node_has_telemetry && last_field_live_ts >= last_set_ts;
    std::string delivery_hint;
    if (last_set_csv.empty()) {
        delivery_hint = "Ingen set_config endnu siden serverstart.";
    } else if (!profiles_match) {
        delivery_hint = "Aktiv profilvalgelse matcher ikke sidste set_config-koe.";
    } else if (!last_set_queued_ok) {
        delivery_hint = "Sidste set_config kom ikke i send-koeen.";
    } else if (!client_ok) {
        delivery_hint = "Sidste set_config matcher gemt aktiv konfiguration; klient er frakoblet (besked ligger klar i koeen).";
    } else if (!field_node_has_telemetry) {
        delivery_hint = "set_config er sendt; afventer field_node telemetri fra Pi 2.";
    } else if (!field_node_up_to_date_for_last_set) {
        delivery_hint = "set_config er sendt; afventer ny Pi 2 telemetri med opdateret field_node-status.";
    } else {
        delivery_hint = "Pi 2 field_node er opdateret efter sidste set_config.";
    }

    std::string field_node_body;
    if (last_field_live_ts == "never") {
        field_node_body =
            "\"telemetry_ts\": \"never\",\n"
            "    \"components\": null,\n"
            "    \"hint\": \"" +
            web_ui_json_escape("Ingen Pi 2 telemetri med field_live endnu (eller aeldre klient).") + "\"";
    } else {
        field_node_body =
            "\"telemetry_ts\": \"" + web_ui_json_escape(last_field_live_ts) + "\",\n"
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
           "  \"last_hello_ts\": \"" + web_ui_json_escape(last_hello) + "\",\n"
           "  \"last_control\": {\n"
           "    \"action\": \"" + web_ui_json_escape(last_control_action) + "\",\n"
           "    \"queued\": " + std::string(last_control_queued ? "true" : "false") + ",\n"
           "    \"ts\": \"" + web_ui_json_escape(last_control_ts) + "\"\n"
           "  },\n"
           "  \"last_set_config\": {\n"
           "    \"profile_name\": \"" + web_ui_json_escape(last_set_csv) + "\",\n"
           "    \"queued_ok\": " + std::string(last_set_queued_ok ? "true" : "false") + ",\n"
           "    \"ts\": \"" + web_ui_json_escape(last_set_ts) + "\"\n"
           "  },\n"
           "  \"config_queue_matches_active\": " + std::string(queue_matches_active ? "true" : "false") + ",\n"
           "  \"config_delivery_ok\": " + std::string(delivery_ok ? "true" : "false") + ",\n"
           "  \"field_node_up_to_date_for_last_set_config\": "
           + std::string(field_node_up_to_date_for_last_set ? "true" : "false")
           + ",\n"
           "  \"config_delivery_hint\": \"" + web_ui_json_escape(delivery_hint) + "\",\n"
           "  \"field_node\": {\n"
           "    " +
            field_node_body + "\n"
                              "  }\n"
                              "}\n";
}

std::string WebUiServer::build_config_json() const {
    const auto cfg = config_manager_.current_config();
    const auto profiles = web_ui_dedupe_preserve_order(web_ui_split_csv(cfg.profile_name));
    const std::string saved_at = db_manager_.read_active_config_timestamp_utc();
    std::ostringstream oss;
    oss << "{\n"
        << "  \"profile_name\": \"" << web_ui_json_escape(cfg.profile_name) << "\",\n"
        << "  \"profile_names\": [";
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        oss << "\"" << web_ui_json_escape(profiles[i]) << "\"";
        if (i + 1 < profiles.size()) {
            oss << ", ";
        }
    }
    oss << "],\n"
        << "  \"interval_sec\": " << cfg.interval_sec << ",\n"
        << "  \"emergency_stop\": " << (cfg.emergency_stop ? "true" : "false") << ",\n"
        << "  \"ventilation_enabled\": " << (cfg.ventilation_enabled ? "true" : "false") << ",\n"
        << "  \"watering_enabled\": " << (cfg.watering_enabled ? "true" : "false") << ",\n"
        << "  \"mode\": \"" << web_ui_json_escape(to_string(cfg.mode)) << "\",\n"
        << "  \"saved_at\": \"" << web_ui_json_escape(saved_at) << "\"\n"
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
            << "    \"name\": \"" << web_ui_json_escape(p.name) << "\",\n"
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
            << "    \"label\": \"" << web_ui_json_escape(box.label) << "\",\n"
            << "    \"slot\": " << box.slot << ",\n"
            << "    \"is_active\": " << (box.is_active ? "true" : "false") << ",\n"
            << "    \"is_default\": " << (box.is_default ? "true" : "false") << ",\n"
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
