#include "datahaandtering/web_ui_server.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <cstddef>
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

namespace datahaandtering {
namespace {

#if defined(_WIN32)
std::string last_socket_error() {
    return std::to_string(WSAGetLastError());
}
#else
std::string last_socket_error() {
    return std::strerror(errno);
}
#endif

void close_socket(int socket_fd) {
#if defined(_WIN32)
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

std::unordered_map<std::string, std::string> parse_query(std::string_view request_line) {
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

std::string json_escape(const std::string& s) {
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

std::vector<std::string> split_csv(const std::string& value) {
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

std::vector<std::string> dedupe_preserve_order(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& item : items) {
        if (seen.insert(item).second) {
            out.push_back(item);
        }
    }
    return out;
}

std::string join_csv(const std::vector<std::string>& items) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < items.size(); ++i) {
        oss << items[i];
        if (i + 1 < items.size()) {
            oss << ",";
        }
    }
    return oss.str();
}

std::set<std::string> profile_name_set(const std::vector<shared::Profile>& profiles) {
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

bool aggregate_bounds_if_compatible(
    const std::vector<shared::Profile>& selected_profiles,
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

}  // namespace

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
        shared::log_error("web_ui", "WSAStartup failed");
        running_ = false;
        return false;
    }
#endif

    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ < 0) {
        shared::log_error("web_ui", "Failed to create socket: " + last_socket_error());
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
        shared::log_error("web_ui", "bind failed: " + last_socket_error());
        close_socket(listen_socket_);
        listen_socket_ = -1;
#if defined(_WIN32)
        WSACleanup();
#endif
        running_ = false;
        return false;
    }

    if (listen(listen_socket_, 8) < 0) {
        shared::log_error("web_ui", "listen failed: " + last_socket_error());
        close_socket(listen_socket_);
        listen_socket_ = -1;
#if defined(_WIN32)
        WSACleanup();
#endif
        running_ = false;
        return false;
    }

    accept_thread_ = std::thread(&WebUiServer::accept_loop, this);
    shared::log_info("web_ui", "Browser UI listening on port " + std::to_string(port_));
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
    shared::log_info("web_ui", "Browser UI stopped");
}

void WebUiServer::note_client_hello(const std::string& timestamp_utc) {
    hello_count_.fetch_add(1);
    std::lock_guard<std::mutex> lock(status_mutex_);
    last_hello_ts_ = timestamp_utc;
}

void WebUiServer::accept_loop() {
    while (running_) {
        sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        const int client_socket =
            accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) {
            if (running_) {
                shared::log_warn("web_ui", "accept failed: " + last_socket_error());
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
            std::vector<shared::Profile> selected_profiles;
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
        if (const auto it = query.find("interval"); it != query.end()) {
            try {
                config_manager_.set_interval_sec(std::stoi(it->second));
                changed = true;
            } catch (...) {
                status_line = "HTTP/1.1 400 Bad Request\r\n";
                body = "{\"error\":\"invalid interval\"}\n";
                return true;
            }
        }
        if (const auto it = query.find("emergency"); it != query.end()) {
            const bool enabled = (it->second == "1" || it->second == "true");
            config_manager_.set_emergency_stop(enabled);
            changed = true;
        }

        if (changed) {
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
        shared::Profile p;
        if (const auto it = query.find("name"); it != query.end()) {
            p.name = it->second;
        }
        if (const auto it = query.find("crop"); it != query.end()) {
            p.crop_type = it->second;
        }
        if (p.name.empty() || p.crop_type.empty()) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"name and crop required\"}\n";
            return true;
        }
        try {
            p.min_humidity_air = std::stod(query.at("min_air"));
            p.max_humidity_air = std::stod(query.at("max_air"));
            p.min_humidity_soil = std::stod(query.at("min_soil"));
            p.max_humidity_soil = std::stod(query.at("max_soil"));
            p.interval_sec = std::stoi(query.at("interval"));
        } catch (...) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"invalid numeric profile field\"}\n";
            return true;
        }
        p.is_default = false;
        const bool ok = db_manager_.upsert_profile(p, shared::now_utc_iso8601());
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
        shared::PlantBoxConfig plant_box;
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
        if (plant_box.label.empty() || plant_box.slot < 1) {
            status_line = "HTTP/1.1 400 Bad Request\r\n";
            body = "{\"error\":\"label and slot>=1 required\"}\n";
            return true;
        }
        const bool ok = db_manager_.upsert_plant_box(plant_box, shared::now_utc_iso8601());
        const bool queued = ok ? command_dispatcher_.dispatch_current_config() : false;
        if (ok) {
            note_control_result("set_config", queued);
        }
        body = std::string("{\"ok\":")
            + (ok ? "true" : "false")
            + ",\"queued\":"
            + (queued ? "true" : "false")
            + "}\n";
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
    std::lock_guard<std::mutex> lock(status_mutex_);
    last_control_action_ = std::move(action);
    last_control_queued_ = queued;
    last_control_ts_ = shared::now_utc_iso8601();
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
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        last_hello = last_hello_ts_;
        last_control_action = last_control_action_;
        last_control_ts = last_control_ts_;
        last_control_queued = last_control_queued_;
    }
    return "{\n"
           "  \"service\": \"datahaandtering\",\n"
           "  \"ui\": \"running\",\n"
           "  \"client_connected\": "
           + std::string(tcp_server_.is_client_connected() ? "true" : "false")
           + ",\n"
           "  \"hello_count\": " + std::to_string(hello_count_.load()) + ",\n"
           "  \"last_hello_ts\": \"" + last_hello + "\",\n"
           "  \"last_control\": {\n"
           "    \"action\": \"" + last_control_action + "\",\n"
           "    \"queued\": " + std::string(last_control_queued ? "true" : "false") + ",\n"
           "    \"ts\": \"" + last_control_ts + "\"\n"
           "  }\n"
           "}\n";
}

std::string WebUiServer::build_config_json() const {
    const auto cfg = config_manager_.current_config();
    const auto profiles = dedupe_preserve_order(split_csv(cfg.profile_name));
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
        << "  \"emergency_stop\": " << (cfg.emergency_stop ? "true" : "false") << "\n"
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
            << "    \"crop_type\": \"" << json_escape(p.crop_type) << "\",\n"
            << "    \"min_humidity_air\": " << p.min_humidity_air << ",\n"
            << "    \"max_humidity_air\": " << p.max_humidity_air << ",\n"
            << "    \"min_humidity_soil\": " << p.min_humidity_soil << ",\n"
            << "    \"max_humidity_soil\": " << p.max_humidity_soil << ",\n"
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
            << "    \"is_active\": " << (box.is_active ? "true" : "false") << "\n"
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
           "    button { border: 1px solid #15803d; border-radius: 8px; background: #16a34a; color: #fff; padding: 0.42rem 0.7rem; cursor: pointer; display: inline-flex; align-items: center; justify-content: center; text-align: center; }\n"
           "    button.secondary { border-color: #eab308; background: #fefce8; color: #854d0e; }\n"
           "    button.danger { border-color: #dc2626; background: #ef4444; }\n"
           "    .kpi { display: grid; gap: 0.5rem; grid-template-columns: repeat(2, minmax(0, 1fr)); }\n"
           "    .pill { display: inline-block; padding: 0.2rem 0.55rem; border-radius: 999px; font-size: 0.75rem; background: #e5e7eb; }\n"
           "    .pill.ok { background: #dcfce7; color: #166534; }\n"
           "    .pill.warn { background: #fef3c7; color: #92400e; }\n"
           "    .pill.live { background: #dcfce7; color: #166534; }\n"
           "    .pill.draft { background: #fef3c7; color: #92400e; }\n"
           "    .box { background: #f9fafb; border: 1px solid #e5e7eb; border-radius: 10px; padding: 0.65rem; }\n"
           "    .hint { font-size: 0.85rem; color: #6b7280; margin-top: 0.35rem; }\n"
           "    .warn { color: #92400e; }\n"
           "    .section-gap { margin-top: 0.8rem; }\n"
           "    .info-panel { margin-top: 0.75rem; background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 10px; padding: 0.7rem; }\n"
           "    .info-panel h3 { margin: 0 0 0.35rem 0; font-size: 0.98rem; color: #166534; }\n"
           "    .info-panel ul { margin: 0; padding-left: 1rem; color: #334155; font-size: 0.82rem; line-height: 1.35; }\n"
           "    .info-panel li { margin: 0.2rem 0; }\n"
           "    pre { background: #f3f4f6; padding: 0.75rem; border-radius: 8px; overflow: auto; font-size: 0.84rem; }\n"
           "    table { width: 100%; border-collapse: collapse; font-size: 0.9rem; }\n"
           "    th, td { text-align: left; padding: 0.45rem; border-bottom: 1px solid #e5e7eb; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <div class=\"container\">\n"
           "    <h1>Optihus Kontrolpanel</h1>\n"
           "    <p class=\"muted\">Profilvalg og drivkasser redigeres i sektionerne nedenfor og bliver først aktivt efter gemte ændringer er sendt.</p>\n"
           "    <div class=\"grid\">\n"
           "      <section class=\"card\">\n"
           "        <h2>Systemstatus</h2>\n"
           "        <div class=\"kpi\" id=\"kpi\"></div>\n"
           "        <pre id=\"statusRaw\">Indlæser...</pre>\n"
           "      </section>\n"
           "      <section class=\"card\">\n"
           "        <h2>Styring</h2>\n"
           "        <div class=\"row section-gap\">\n"
           "          <button onclick=\"sendControl('/api/control/emergency-stop?enabled=1')\" class=\"danger\">Nødstop</button>\n"
           "          <button onclick=\"sendControl('/api/control/emergency-stop?enabled=0')\" class=\"secondary\">Fortsæt drift</button>\n"
           "          <button onclick=\"sendControl('/api/control/sync-config')\">Send aktiv konfiguration igen</button>\n"
           "          <button onclick=\"openGrafana()\" class=\"secondary\">Åbn Grafana (read-only)</button>\n"
           "        </div>\n"
           "        <p class=\"hint\">\"Send aktiv konfiguration igen\" bruges hvis klienten mister sidste konfigurationsbesked og skal have den sendt på ny. Grafana bruges kun som read-only overblik.</p>\n"
           "        <pre id=\"control\">Ingen handling endnu.</pre>\n"
           "      </section>\n"
           "      <section class=\"card\">\n"
           "        <h2>Forklaring af indstillinger</h2>\n"
           "        <div class=\"info-panel\">\n"
           "          <ul>\n"
           "            <li><strong>Interval:</strong> hvor ofte styringen evaluerer regler og sender opdateringer.</li>\n"
           "            <li><strong>Nødstop:</strong> sikkerhedsindstilling, der stopper automatisk drift indtil du fortsætter.</li>\n"
           "            <li><strong>Profilnavn:</strong> unikt navn til at vælge profilen i aktiv konfiguration.</li>\n"
           "            <li><strong>Afgrøde-type:</strong> læsbart kategori-navn (fx Tomat) til overblik.</li>\n"
           "            <li><strong>Minimum/Maksimum luftfugtighed:</strong> nedre/øvre grænser for luftfugtighed.</li>\n"
           "            <li><strong>Minimum/Maksimum jordfugtighed:</strong> nedre/øvre grænser for jordfugtighed og vanding.</li>\n"
           "            <li><strong>Profil-interval:</strong> anbefalet kontrolinterval for den enkelte profil.</li>\n"
           "            <li><strong>Drivkasse:</strong> logisk kasse/slot, der beskriver hvilken fysisk plantekasse konfigurationen hører til.</li>\n"
           "          </ul>\n"
           "        </div>\n"
           "      </section>\n"
           "      <section class=\"card\" style=\"grid-column: 1 / -1;\">\n"
           "        <h2>Aktiv konfiguration</h2>\n"
           "        <div class=\"row\" style=\"margin-top:0.6rem;\">\n"
           "          <div class=\"field\"><label>Interval (sekunder)</label><input id=\"interval\" type=\"number\" min=\"1\" value=\"900\"></div>\n"
           "          <div class=\"field\"><label>Nødstop</label><select id=\"emergency\"><option value=\"0\">fra</option><option value=\"1\">til</option></select></div>\n"
           "          <button onclick=\"saveConfig()\">Gem konfiguration</button>\n"
           "        </div>\n"
           "        <p class=\"hint\">Konfiguration styres af valgte profiler, interval og nødstop. Ved flere profiler beregnes fælles grænser automatisk (højeste minimum og laveste maksimum). Hvis profilerne ikke overlapper, afvises konfigurationen.</p>\n"
           "        <div class=\"row\" style=\"margin-top:0.6rem;\">\n"
           "          <div class=\"field\" style=\"min-width:320px; flex:1;\"><label>Aktiv (kører nu)</label><div id=\"activeSummary\" class=\"box\">Indlæser...</div></div>\n"
           "          <div class=\"field\" style=\"min-width:320px; flex:1;\"><label>Kladde (ikke gemt endnu)</label><div id=\"draftSummary\" class=\"box\">Indlæser...</div></div>\n"
           "        </div>\n"
           "        <div class=\"hint warn\" id=\"profileRequirementMsg\"></div>\n"
           "      </section>\n"
           "      <section class=\"card\" style=\"grid-column: 1 / -1;\">\n"
           "        <h2>Profiler</h2>\n"
           "        <div class=\"row\">\n"
           "          <div class=\"field\"><label>Profilnavn</label><input id=\"p_name\" placeholder=\"custom1\"></div>\n"
           "          <div class=\"field\"><label>Afgrøde-type</label><input id=\"p_crop\" placeholder=\"Custom\"></div>\n"
           "          <div class=\"field\"><label>Minimum luftfugtighed</label><input id=\"p_min_air\" type=\"number\" step=\"0.1\" value=\"50\"></div>\n"
           "          <div class=\"field\"><label>Maksimum luftfugtighed</label><input id=\"p_max_air\" type=\"number\" step=\"0.1\" value=\"70\"></div>\n"
           "          <div class=\"field\"><label>Minimum jordfugtighed</label><input id=\"p_min_soil\" type=\"number\" step=\"0.1\" value=\"30\"></div>\n"
           "          <div class=\"field\"><label>Maksimum jordfugtighed</label><input id=\"p_max_soil\" type=\"number\" step=\"0.1\" value=\"55\"></div>\n"
           "          <div class=\"field\"><label>Interval (sekunder)</label><input id=\"p_interval\" type=\"number\" min=\"1\" value=\"900\"></div>\n"
           "          <button onclick=\"saveProfile()\">Gem profil</button>\n"
           "        </div>\n"
           "        <div style=\"overflow:auto; margin-top:0.8rem;\">\n"
           "          <table>\n"
           "            <thead><tr><th>Navn</th><th>Afgrøde</th><th>Luft min/maks</th><th>Jord min/maks</th><th>Interval</th><th>Handlinger</th></tr></thead>\n"
           "            <tbody id=\"profilesBody\"><tr><td colspan=\"6\">Indlæser...</td></tr></tbody>\n"
           "          </table>\n"
           "        </div>\n"
           "      </section>\n"
           "      <section class=\"card\" style=\"grid-column: 1 / -1;\">\n"
           "        <h2>Drivkasser</h2>\n"
           "        <p class=\"hint\">Definer logiske drivkasser her. I nuværende hardware bruges kun slot 1 fysisk på Pi 2, men hele listen gemmes og sendes i set_config for fremtidig multi-kasse støtte.</p>\n"
           "        <div class=\"row\">\n"
           "          <div class=\"field\"><label>Drivkasse-id (tom = ny)</label><input id=\"pb_id\" type=\"number\" min=\"1\" placeholder=\"auto\"></div>\n"
           "          <div class=\"field\"><label>Label</label><input id=\"pb_label\" placeholder=\"Drivkasse 1\"></div>\n"
           "          <div class=\"field\"><label>Slot</label><input id=\"pb_slot\" type=\"number\" min=\"1\" value=\"1\"></div>\n"
           "          <div class=\"field\"><label>Aktiv</label><select id=\"pb_active\"><option value=\"1\">ja</option><option value=\"0\">nej</option></select></div>\n"
           "          <button onclick=\"savePlantBox()\">Gem drivkasse</button>\n"
           "        </div>\n"
           "        <div style=\"overflow:auto; margin-top:0.8rem;\">\n"
           "          <table>\n"
           "            <thead><tr><th>ID</th><th>Label</th><th>Slot</th><th>Aktiv</th><th>Handlinger</th></tr></thead>\n"
           "            <tbody id=\"plantBoxesBody\"><tr><td colspan=\"5\">Indlæser...</td></tr></tbody>\n"
           "          </table>\n"
           "        </div>\n"
           "      </section>\n"
           "    </div>\n"
           "  </div>\n"
           "  <script>\n"
           "    let latestProfiles = [];\n"
           "    let latestPlantBoxes = [];\n"
           "    let activeConfig = null;\n"
           "    const draftConfig = { profiles: [], interval_sec: 900, emergency_stop: false };\n"
           "    let draftDirty = false;\n"
           "    function escapeHtml(value) {\n"
           "      return String(value ?? '').replace(/[&<>\\\"']/g, ch => ({'&':'&amp;','<':'&lt;','>':'&gt;','\\\"':'&quot;',\"'\":'&#39;'}[ch]));\n"
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
           "    function activeProfiles() {\n"
           "      if (!activeConfig) return [];\n"
           "      if (Array.isArray(activeConfig.profile_names)) return dedupe(activeConfig.profile_names);\n"
           "      return parseCsvProfiles(activeConfig.profile_name || '');\n"
           "    }\n"
           "    function syncDraftFromActive() {\n"
           "      if (!activeConfig) return;\n"
           "      draftConfig.profiles = activeProfiles();\n"
           "      draftConfig.interval_sec = Number(activeConfig.interval_sec || 900);\n"
           "      draftConfig.emergency_stop = !!activeConfig.emergency_stop;\n"
           "      document.getElementById('interval').value = String(draftConfig.interval_sec);\n"
           "      document.getElementById('emergency').value = draftConfig.emergency_stop ? '1' : '0';\n"
           "    }\n"
           "    function renderConfigViews() {\n"
           "      const active = activeProfiles();\n"
           "      const activeSummary = document.getElementById('activeSummary');\n"
           "      const draftSummary = document.getElementById('draftSummary');\n"
           "      const requirement = document.getElementById('profileRequirementMsg');\n"
           "      activeSummary.innerHTML = `Profiler: ${escapeHtml(active.join(', ') || 'ingen')}<br>Interval: ${escapeHtml(activeConfig?.interval_sec || 900)} sek<br>Nødstop: ${activeConfig?.emergency_stop ? 'TIL' : 'FRA'}`;\n"
           "      draftSummary.innerHTML = `Profiler: ${escapeHtml(draftConfig.profiles.join(', ') || 'ingen')}<br>Interval: ${escapeHtml(draftConfig.interval_sec)} sek<br>Nødstop: ${draftConfig.emergency_stop ? 'TIL' : 'FRA'}`;\n"
           "      if (draftConfig.profiles.length === 0) {\n"
           "        requirement.textContent = 'Du skal vælge mindst én profil i Profiler-sektionen før konfigurationen kan gemmes og køre.';\n"
           "      } else {\n"
           "        requirement.textContent = '';\n"
           "      }\n"
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
           "      renderProfilesTable();\n"
           "      renderConfigViews();\n"
           "    }\n"
           "    function renderProfilesTable() {\n"
           "      const body = document.getElementById('profilesBody');\n"
           "      if (!Array.isArray(latestProfiles) || latestProfiles.length === 0) {\n"
           "        body.innerHTML = '<tr><td colspan=\"6\">Ingen profiler</td></tr>';\n"
           "        return;\n"
           "      }\n"
           "      body.innerHTML = latestProfiles.map(p => {\n"
           "        const chosen = draftConfig.profiles.includes(p.name);\n"
           "        const toggleLabel = chosen ? 'Fjern' : 'Vælg';\n"
           "        const toggleClass = chosen ? 'secondary' : '';\n"
           "        return `\n"
           "          <tr>\n"
           "            <td>${escapeHtml(p.name)}</td>\n"
           "            <td>${escapeHtml(p.crop_type)}</td>\n"
           "            <td>${escapeHtml(p.min_humidity_air)} / ${escapeHtml(p.max_humidity_air)}</td>\n"
           "            <td>${escapeHtml(p.min_humidity_soil)} / ${escapeHtml(p.max_humidity_soil)}</td>\n"
           "            <td>${escapeHtml(p.interval_sec)}</td>\n"
           "            <td>\n"
           "              <button class=\"${toggleClass}\" onclick=\"toggleProfile('${encodeURIComponent(p.name)}')\">${toggleLabel}</button>\n"
           "              <button class=\"danger\" onclick=\"deleteProfile('${encodeURIComponent(p.name)}')\" ${p.is_default ? 'disabled title=\"Standardprofiler kan ikke slettes\" style=\"opacity:0.5;cursor:not-allowed\"' : ''}>Slet</button>\n"
           "            </td>\n"
           "          </tr>`;\n"
           "      }).join('');\n"
           "    }\n"
           "    function editPlantBox(encodedId) {\n"
           "      const id = Number(decodeURIComponent(encodedId));\n"
           "      const current = latestPlantBoxes.find(pb => Number(pb.id) === id);\n"
           "      if (!current) return;\n"
           "      document.getElementById('pb_id').value = String(current.id);\n"
           "      document.getElementById('pb_label').value = String(current.label || '');\n"
           "      document.getElementById('pb_slot').value = String(current.slot || 1);\n"
           "      document.getElementById('pb_active').value = current.is_active ? '1' : '0';\n"
           "    }\n"
           "    function renderPlantBoxesTable() {\n"
           "      const body = document.getElementById('plantBoxesBody');\n"
           "      if (!Array.isArray(latestPlantBoxes) || latestPlantBoxes.length === 0) {\n"
           "        body.innerHTML = '<tr><td colspan=\"5\">Ingen drivkasser</td></tr>';\n"
           "        return;\n"
           "      }\n"
           "      body.innerHTML = latestPlantBoxes.map(pb => `\n"
           "        <tr>\n"
           "          <td>${escapeHtml(pb.id)}</td>\n"
           "          <td>${escapeHtml(pb.label)}</td>\n"
           "          <td>${escapeHtml(pb.slot)}</td>\n"
           "          <td>${pb.is_active ? 'ja' : 'nej'}</td>\n"
           "          <td>\n"
           "            <button class=\"secondary\" onclick=\"editPlantBox('${encodeURIComponent(String(pb.id))}')\">Redigér</button>\n"
           "            <button class=\"danger\" onclick=\"deletePlantBox('${encodeURIComponent(String(pb.id))}')\">Slet</button>\n"
           "          </td>\n"
           "        </tr>\n"
           "      `).join('');\n"
           "    }\n"
           "    async function loadStatus() {\n"
           "      try {\n"
           "        const res = await fetch('/api/status');\n"
           "        const json = await res.json();\n"
           "        document.getElementById('statusRaw').textContent = JSON.stringify(json, null, 2);\n"
           "        const connected = json.client_connected;\n"
           "        document.getElementById('kpi').innerHTML = `\n"
           "          <div><strong>Service:</strong> ${escapeHtml(json.service)}</div>\n"
           "          <div><strong>UI:</strong> ${escapeHtml(json.ui)}</div>\n"
           "          <div><strong>Klient:</strong> <span class=\"pill ${connected ? 'ok' : 'warn'}\">${connected ? 'tilkoblet' : 'frakoblet'}</span></div>\n"
           "          <div><strong>Antal hello:</strong> ${escapeHtml(json.hello_count)}</div>\n"
           "          <div><strong>Seneste hello:</strong> ${escapeHtml(json.last_hello_ts)}</div>\n"
           "          <div><strong>Seneste handling:</strong> ${escapeHtml(json.last_control?.action || 'ingen')}</div>\n"
           "        `;\n"
           "      } catch (e) {\n"
           "        document.getElementById('statusRaw').textContent = 'Status utilgængelig: ' + e;\n"
           "      }\n"
           "    }\n"
           "    async function loadConfig() {\n"
           "      try {\n"
           "        const res = await fetch('/api/config');\n"
           "        activeConfig = await res.json();\n"
           "        if (!draftDirty) {\n"
           "          syncDraftFromActive();\n"
           "        }\n"
           "        renderConfigViews();\n"
           "      } catch (e) {\n"
           "        document.getElementById('activeSummary').textContent = 'Konfiguration utilgængelig: ' + e;\n"
           "      }\n"
           "    }\n"
           "    async function loadProfiles() {\n"
           "      try {\n"
           "        const res = await fetch('/api/profiles');\n"
           "        latestProfiles = await res.json();\n"
           "        if (!Array.isArray(latestProfiles)) latestProfiles = [];\n"
           "        const available = new Set(latestProfiles.map(p => p.name));\n"
           "        draftConfig.profiles = draftConfig.profiles.filter(p => available.has(p));\n"
           "        renderProfilesTable();\n"
           "        renderConfigViews();\n"
           "      } catch (e) {\n"
           "        document.getElementById('profilesBody').innerHTML = '<tr><td colspan=\"6\">Profiler utilgængelige: ' + e + '</td></tr>';\n"
           "      }\n"
           "    }\n"
           "    async function loadPlantBoxes() {\n"
           "      try {\n"
           "        const res = await fetch('/api/plant-boxes');\n"
           "        latestPlantBoxes = await res.json();\n"
           "        if (!Array.isArray(latestPlantBoxes)) latestPlantBoxes = [];\n"
           "        renderPlantBoxesTable();\n"
           "      } catch (e) {\n"
           "        document.getElementById('plantBoxesBody').innerHTML = '<tr><td colspan=\"5\">Drivkasser utilgængelige: ' + e + '</td></tr>';\n"
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
           "      const interval = encodeURIComponent(String(draftConfig.interval_sec));\n"
           "      const emergency = encodeURIComponent(draftConfig.emergency_stop ? '1' : '0');\n"
           "      const result = await sendControl(`/api/config?profiles=${profilesParam}&interval=${interval}&emergency=${emergency}`);\n"
           "      if (result?.ok) {\n"
           "        draftDirty = false;\n"
           "      }\n"
           "      await loadConfig();\n"
           "    }\n"
           "    async function saveProfile() {\n"
           "      const name = encodeURIComponent(document.getElementById('p_name').value);\n"
           "      const crop = encodeURIComponent(document.getElementById('p_crop').value);\n"
           "      const min_air = encodeURIComponent(document.getElementById('p_min_air').value);\n"
           "      const max_air = encodeURIComponent(document.getElementById('p_max_air').value);\n"
           "      const min_soil = encodeURIComponent(document.getElementById('p_min_soil').value);\n"
           "      const max_soil = encodeURIComponent(document.getElementById('p_max_soil').value);\n"
           "      const interval = encodeURIComponent(document.getElementById('p_interval').value);\n"
           "      await sendControl(`/api/profiles/upsert?name=${name}&crop=${crop}&min_air=${min_air}&max_air=${max_air}&min_soil=${min_soil}&max_soil=${max_soil}&interval=${interval}`);\n"
           "      await loadProfiles();\n"
           "    }\n"
           "    async function savePlantBox() {\n"
           "      const idRaw = document.getElementById('pb_id').value.trim();\n"
           "      const label = document.getElementById('pb_label').value.trim();\n"
           "      const slot = document.getElementById('pb_slot').value.trim();\n"
           "      const isActive = document.getElementById('pb_active').value === '1' ? '1' : '0';\n"
           "      if (!label) {\n"
           "        alert('Label er påkrævet for drivkasse.');\n"
           "        return;\n"
           "      }\n"
           "      const parts = [];\n"
           "      if (idRaw) parts.push(`id=${encodeURIComponent(idRaw)}`);\n"
           "      parts.push(`label=${encodeURIComponent(label)}`);\n"
           "      parts.push(`slot=${encodeURIComponent(slot || '1')}`);\n"
           "      parts.push(`is_active=${encodeURIComponent(isActive)}`);\n"
           "      await sendControl(`/api/plant-boxes/upsert?${parts.join('&')}`);\n"
           "      document.getElementById('pb_id').value = '';\n"
           "      document.getElementById('pb_label').value = '';\n"
           "      document.getElementById('pb_slot').value = '1';\n"
           "      document.getElementById('pb_active').value = '1';\n"
           "      await loadPlantBoxes();\n"
           "    }\n"
           "    async function deletePlantBox(encodedId) {\n"
           "      const id = decodeURIComponent(encodedId);\n"
           "      if (!confirm(`Slet drivkasse med id ${id}?`)) return;\n"
           "      const result = await sendControl(`/api/plant-boxes/delete?id=${encodeURIComponent(id)}`);\n"
           "      if (result?.error) {\n"
           "        alert(`Kunne ikke slette drivkasse: ${result.error}`);\n"
           "      }\n"
           "      await loadPlantBoxes();\n"
           "    }\n"
           "    async function sendControl(url) {\n"
           "      const target = document.getElementById('control');\n"
           "      target.textContent = 'Sender...';\n"
           "      try {\n"
           "        const res = await fetch(url, { method: 'POST' });\n"
           "        const json = await res.json();\n"
           "        target.textContent = JSON.stringify(json, null, 2);\n"
           "        loadStatus();\n"
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
           "    document.getElementById('interval').addEventListener('input', (e) => {\n"
           "      draftConfig.interval_sec = Number(e.target.value || 900);\n"
           "      draftDirty = true;\n"
           "      renderConfigViews();\n"
           "    });\n"
           "    document.getElementById('emergency').addEventListener('change', (e) => {\n"
           "      draftConfig.emergency_stop = e.target.value === '1';\n"
           "      draftDirty = true;\n"
           "      renderConfigViews();\n"
           "    });\n"
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

}  // namespace datahaandtering
