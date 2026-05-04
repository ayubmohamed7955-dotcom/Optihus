#include "datahaandtering/tcp_server.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "shared/logger.hpp"
#include "shared/message_codec.hpp"

namespace datahaandtering {

TcpServer::TcpServer(
    int port,
    shared::ThreadSafeQueue<shared::MessageEnvelope>& inbound_queue,
    shared::ThreadSafeQueue<shared::MessageEnvelope>& outbound_queue
)
    : port_(port), inbound_queue_(inbound_queue), outbound_queue_(outbound_queue) {}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    if (port_ <= 0 || running_.exchange(true)) {
        return false;
    }

    listen_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket_ < 0) {
        shared::log_error("tcp_server", "Failed to create socket");
        running_ = false;
        return false;
    }

    int reuse = 1;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port_));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        shared::log_error("tcp_server", "bind failed: " + std::string(std::strerror(errno)));
        close(listen_socket_);
        listen_socket_ = -1;
        running_ = false;
        return false;
    }

    if (listen(listen_socket_, 1) < 0) {
        shared::log_error("tcp_server", "listen failed");
        close(listen_socket_);
        listen_socket_ = -1;
        running_ = false;
        return false;
    }

    accept_thread_ = std::thread(&TcpServer::accept_loop, this);
    shared::log_info("tcp_server", "Listening on port " + std::to_string(port_));
    return true;
}

void TcpServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (listen_socket_ >= 0) {
        shutdown(listen_socket_, SHUT_RDWR);
        close(listen_socket_);
        listen_socket_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    stop_session();

    shared::log_info("tcp_server", "Server stopped");
}

void TcpServer::accept_loop() {
    while (running_) {
        sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        const int client_socket =
            accept(listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_socket < 0) {
            if (running_) {
                shared::log_warn("tcp_server", "accept failed");
            }
            continue;
        }

        start_session(client_socket);

        char ip_buffer[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buffer, sizeof(ip_buffer));
        shared::log_info("tcp_server", "Client connected: " + std::string(ip_buffer));
    }
}

void TcpServer::start_session(int client_socket) {
    std::lock_guard<std::mutex> lock(session_mutex_);
    stop_session();
    client_socket_ = client_socket;
    session_running_ = true;
    read_thread_ = std::thread(&TcpServer::read_loop, this);
    write_thread_ = std::thread(&TcpServer::write_loop, this);
}

void TcpServer::stop_session() {
    if (!session_running_.exchange(false)) {
        return;
    }

    if (client_socket_ >= 0) {
        shutdown(client_socket_, SHUT_RDWR);
    }
    outbound_queue_.push(shared::make_message(shared::MessageType::Event, "datahaandtering"));

    if (read_thread_.joinable()) {
        read_thread_.join();
    }
    if (write_thread_.joinable()) {
        write_thread_.join();
    }

    if (client_socket_ >= 0) {
        close(client_socket_);
        client_socket_ = -1;
    }
}

void TcpServer::read_loop() {
    std::string buffer;
    buffer.reserve(4096);
    char chunk[1024];

    while (session_running_) {
        const ssize_t n = recv(client_socket_, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            if (n < 0) {
                shared::log_warn("tcp_server", "recv failed: " + std::string(std::strerror(errno)));
            }
            session_running_ = false;
            break;
        }

        buffer.append(chunk, static_cast<std::size_t>(n));
        std::size_t newline_pos = std::string::npos;
        while ((newline_pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, newline_pos);
            buffer.erase(0, newline_pos + 1);
            if (line.empty()) {
                continue;
            }

            const auto parsed = shared::decode_json_line(line);
            if (!parsed.has_value()) {
                shared::log_warn("tcp_server", "Dropped invalid JSON line");
                continue;
            }
            inbound_queue_.push(*parsed);
        }
    }
}

void TcpServer::write_loop() {
    while (session_running_) {
        const auto next = outbound_queue_.wait_pop();
        if (!next.has_value()) {
            if (!session_running_) {
                break;
            }
            continue;
        }

        const auto json_line = shared::encode_json_line(*next);
        const char* data = json_line.data();
        std::size_t remaining = json_line.size();
        while (remaining > 0 && session_running_) {
            const ssize_t sent = send(client_socket_, data, remaining, 0);
            if (sent <= 0) {
                shared::log_warn("tcp_server", "send failed, closing session");
                session_running_ = false;
                break;
            }
            data += sent;
            remaining -= static_cast<std::size_t>(sent);
        }
    }
}

}  // namespace datahaandtering
