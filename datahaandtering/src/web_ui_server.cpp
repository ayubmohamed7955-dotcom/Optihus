#include "datahaandtering/web_ui_server.hpp"

#include <array>
#include <algorithm>
#include <cstddef>
#include <string>
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

}  // namespace

WebUiServer::WebUiServer(int port, TcpServer& tcp_server, CommandDispatcher& command_dispatcher)
    : port_(port), tcp_server_(tcp_server), command_dispatcher_(command_dispatcher) {}

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
           "    setInterval(loadStatus, 2000);\n"
           "  </script>\n"
           "</body>\n"
           "</html>\n";
}

}  // namespace datahaandtering
