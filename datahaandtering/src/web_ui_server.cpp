#include "datahaandtering/web_ui_server.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

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
            out[query.substr(start, eq - start)] = query.substr(eq + 1, item_end - (eq + 1));
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
        || request_line.rfind("POST /api/profiles/upsert", 0) == 0) {
        handle_profiles_request(request_line, status_line, body, content_type);
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
        if (const auto it = query.find("mode"); it != query.end()) {
            const auto mode = shared::mode_from_string(it->second);
            if (mode.has_value()) {
                config_manager_.set_mode(*mode);
                changed = true;
            }
        }
        if (const auto it = query.find("profile"); it != query.end() && !it->second.empty()) {
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
    return "{\n"
           "  \"mode\": \"" + shared::to_string(cfg.mode) + "\",\n"
           "  \"profile_name\": \"" + json_escape(cfg.profile_name) + "\",\n"
           "  \"interval_sec\": " + std::to_string(cfg.interval_sec) + ",\n"
           "  \"emergency_stop\": " + std::string(cfg.emergency_stop ? "true" : "false") + "\n"
           "}\n";
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
            << "    \"interval_sec\": " << p.interval_sec << "\n"
            << "  }";
        if (i + 1 < profiles.size()) {
            oss << ",";
        }
        oss << "\n";
    }
    oss << "]\n";
    return oss.str();
}

std::string WebUiServer::build_index_html() {
    return "<!doctype html>\n"
           "<html lang=\"en\">\n"
           "<head>\n"
           "  <meta charset=\"utf-8\">\n"
           "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "  <title>Optihus</title>\n"
           "  <style>\n"
           "    body { font-family: Arial, sans-serif; max-width: 740px; margin: 2rem auto; padding: 0 1rem; }\n"
           "    h1 { margin-bottom: 0.2rem; }\n"
           "    .muted { color: #666; margin-top: 0; }\n"
           "    pre { background: #f4f4f4; padding: 1rem; border-radius: 8px; overflow: auto; }\n"
           "  </style>\n"
           "</head>\n"
           "<body>\n"
           "  <h1>Optihus UI (Stage 3)</h1>\n"
           "  <p class=\"muted\">Minimal browser skeleton running on datahaandtering-rpi.</p>\n"
           "  <h2>Status</h2>\n"
           "  <pre id=\"status\">Loading...</pre>\n"
           "  <h2>Active config</h2>\n"
           "  <pre id=\"config\">Loading...</pre>\n"
           "  <p>\n"
           "    Mode: <select id=\"mode\"><option value=\"auto\">auto</option><option value=\"manual\">manual</option></select>\n"
           "    Profile: <input id=\"profile\" placeholder=\"tomato\">\n"
           "    Interval: <input id=\"interval\" type=\"number\" min=\"1\" value=\"900\" style=\"width:90px\">\n"
           "    Emergency: <select id=\"emergency\"><option value=\"0\">off</option><option value=\"1\">on</option></select>\n"
           "    <button onclick=\"saveConfig()\">Save config</button>\n"
           "  </p>\n"
           "  <h2>Profiles</h2>\n"
           "  <pre id=\"profiles\">Loading...</pre>\n"
           "  <p>\n"
           "    Name: <input id=\"p_name\" placeholder=\"custom1\" style=\"width:90px\">\n"
           "    Crop: <input id=\"p_crop\" placeholder=\"Custom\" style=\"width:90px\">\n"
           "    Min air: <input id=\"p_min_air\" type=\"number\" step=\"0.1\" value=\"50\" style=\"width:70px\">\n"
           "    Max air: <input id=\"p_max_air\" type=\"number\" step=\"0.1\" value=\"70\" style=\"width:70px\">\n"
           "    Min soil: <input id=\"p_min_soil\" type=\"number\" step=\"0.1\" value=\"30\" style=\"width:70px\">\n"
           "    Max soil: <input id=\"p_max_soil\" type=\"number\" step=\"0.1\" value=\"55\" style=\"width:70px\">\n"
           "    Interval: <input id=\"p_interval\" type=\"number\" min=\"1\" value=\"900\" style=\"width:80px\">\n"
           "    <button onclick=\"saveProfile()\">Save profile</button>\n"
           "  </p>\n"
           "  <h2>Controls</h2>\n"
           "  <p>\n"
           "    <button onclick=\"sendControl('/api/control/emergency-stop?enabled=1')\">Emergency stop</button>\n"
           "    <button onclick=\"sendControl('/api/control/emergency-stop?enabled=0')\">Clear emergency stop</button>\n"
           "    <button onclick=\"sendControl('/api/control/sync-config')\">Sync active config</button>\n"
           "  </p>\n"
           "  <pre id=\"control\">No action yet.</pre>\n"
           "  <script>\n"
           "    async function loadStatus() {\n"
           "      try {\n"
           "        const res = await fetch('/api/status');\n"
           "        const json = await res.json();\n"
           "        document.getElementById('status').textContent = JSON.stringify(json, null, 2);\n"
           "      } catch (e) {\n"
           "        document.getElementById('status').textContent = 'Status unavailable: ' + e;\n"
           "      }\n"
           "    }\n"
           "    async function loadConfig() {\n"
           "      try {\n"
           "        const res = await fetch('/api/config');\n"
           "        const json = await res.json();\n"
           "        document.getElementById('config').textContent = JSON.stringify(json, null, 2);\n"
           "        document.getElementById('mode').value = json.mode || 'auto';\n"
           "        document.getElementById('profile').value = json.profile_name || '';\n"
           "        document.getElementById('interval').value = json.interval_sec || 900;\n"
           "        document.getElementById('emergency').value = json.emergency_stop ? '1' : '0';\n"
           "      } catch (e) {\n"
           "        document.getElementById('config').textContent = 'Config unavailable: ' + e;\n"
           "      }\n"
           "    }\n"
           "    async function loadProfiles() {\n"
           "      try {\n"
           "        const res = await fetch('/api/profiles');\n"
           "        const json = await res.json();\n"
           "        document.getElementById('profiles').textContent = JSON.stringify(json, null, 2);\n"
           "      } catch (e) {\n"
           "        document.getElementById('profiles').textContent = 'Profiles unavailable: ' + e;\n"
           "      }\n"
           "    }\n"
           "    async function saveConfig() {\n"
           "      const mode = encodeURIComponent(document.getElementById('mode').value);\n"
           "      const profile = encodeURIComponent(document.getElementById('profile').value);\n"
           "      const interval = encodeURIComponent(document.getElementById('interval').value);\n"
           "      const emergency = encodeURIComponent(document.getElementById('emergency').value);\n"
           "      await sendControl(`/api/config?mode=${mode}&profile=${profile}&interval=${interval}&emergency=${emergency}`);\n"
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
           "    async function sendControl(url) {\n"
           "      const target = document.getElementById('control');\n"
           "      target.textContent = 'Sending...';\n"
           "      try {\n"
           "        const res = await fetch(url, { method: 'POST' });\n"
           "        const json = await res.json();\n"
           "        target.textContent = JSON.stringify(json, null, 2);\n"
           "        loadStatus();\n"
           "      } catch (e) {\n"
           "        target.textContent = 'Control failed: ' + e;\n"
           "      }\n"
           "    }\n"
           "    loadStatus();\n"
           "    loadConfig();\n"
           "    loadProfiles();\n"
           "    setInterval(loadStatus, 2000);\n"
           "    setInterval(loadConfig, 5000);\n"
           "    setInterval(loadProfiles, 10000);\n"
           "  </script>\n"
           "</body>\n"
           "</html>\n";
}

}  // namespace datahaandtering
