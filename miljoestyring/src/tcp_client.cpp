#include "tcp_client.hpp"

#include <string>
#include <utility>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/message_codec.hpp"
#include "shared/time_utils.hpp"

namespace miljoestyring {

#if defined(_WIN32)
std::string last_socket_error() {
    return std::to_string(WSAGetLastError());
}
#else
std::string last_socket_error() {
    return std::strerror(errno);
}
#endif

TcpClient::TcpClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

TcpClient::~TcpClient() {
    disconnect();
#if defined(_WIN32)
    if (winsock_started_) {
        WSACleanup();
        winsock_started_ = false;
    }
#endif
}

void TcpClient::stop_reader() {
    reader_run_.store(false);
    if (reader_thread_.joinable()) {
#if defined(_WIN32)
        if (socket_fd_ != INVALID_SOCKET) {
            shutdown(socket_fd_, SD_BOTH);
        }
#else
        if (socket_fd_ >= 0) {
            shutdown(socket_fd_, SHUT_RDWR);
        }
#endif
        reader_thread_.join();
    }
}

bool TcpClient::connect() {
    if (is_connected()) {
        return true;
    }
    if (host_.empty() || port_ <= 0) {
        shared::log_error("tcp_client", "Invalid server host/port");
        return false;
    }

#if defined(_WIN32)
    if (!winsock_started_) {
        WSADATA wsa_data {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            shared::log_error("tcp_client", "WSAStartup failed");
            return false;
        }
        winsock_started_ = true;
    }
#endif

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (!is_connected()) {
        shared::log_error("tcp_client", "socket() failed: " + last_socket_error());
        return false;
    }

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) != 1) {
        shared::log_error("tcp_client", "Invalid IPv4 address: " + host_);
        disconnect();
        return false;
    }

    if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
        shared::log_error("tcp_client", "connect() failed: " + last_socket_error());
        disconnect();
        return false;
    }

    shared::HelloPayload hello_payload;
    hello_payload.node = std::string(shared::constants::kMiljoeHostname);
    hello_payload.version = "0.1.0";

    const auto hello_message = shared::make_message(
        shared::MessageType::Hello,
        std::string(shared::constants::kSourceMiljoestyring),
        shared::to_json_obj(hello_payload),
        "",
        shared::now_utc_iso8601()
    );

    if (!send_all(shared::encode_json_line(hello_message))) {
        shared::log_error("tcp_client", "Failed to send hello message");
        disconnect();
        return false;
    }

    shared::log_info("tcp_client", "Connected and sent hello to " + host_ + ":" + std::to_string(port_));
    return true;
}

void TcpClient::disconnect() {
    stop_reader();
    read_buffer_.clear();
    if (is_connected()) {
#if defined(_WIN32)
        shutdown(socket_fd_, SD_BOTH);
        closesocket(socket_fd_);
        socket_fd_ = INVALID_SOCKET;
#else
        shutdown(socket_fd_, SHUT_RDWR);
        close(socket_fd_);
        socket_fd_ = -1;
#endif
        shared::log_info("tcp_client", "Disconnected");
    }
}

bool TcpClient::is_connected() const {
#if defined(_WIN32)
    return socket_fd_ != INVALID_SOCKET;
#else
    return socket_fd_ >= 0;
#endif
}

bool TcpClient::is_reader_running() const {
    return reader_run_.load();
}

void TcpClient::start_reader(shared::ThreadSafeQueue<shared::MessageEnvelope>& inbound) {
    stop_reader();
    inbound_queue_ = &inbound;
    read_buffer_.clear();
    reader_run_.store(true);
    reader_thread_ = std::thread(&TcpClient::read_loop, this);
}

void TcpClient::read_loop() {
    char chunk[1024];
    while (reader_run_.load() && is_connected()) {
#if defined(_WIN32)
        const int n = recv(socket_fd_, chunk, static_cast<int>(sizeof(chunk)), 0);
#else
        const ssize_t n = recv(socket_fd_, chunk, sizeof(chunk), 0);
#endif
        if (n <= 0) {
            if (n == 0) {
                shared::log_info("tcp_client", "Server closed connection (recv=0)");
            } else if (reader_run_.load()) {
                shared::log_warn("tcp_client", "recv failed: " + last_socket_error());
            }
            reader_run_.store(false);
            break;
        }

        read_buffer_.append(chunk, static_cast<std::size_t>(n));
        std::size_t newline_pos = std::string::npos;
        while ((newline_pos = read_buffer_.find('\n')) != std::string::npos) {
            std::string line = read_buffer_.substr(0, newline_pos);
            read_buffer_.erase(0, newline_pos + 1);
            if (line.empty()) {
                continue;
            }
            const auto parsed = shared::decode_json_line(line);
            if (!parsed.has_value()) {
                shared::log_warn("tcp_client", "Dropped invalid JSON line from server");
                continue;
            }
            if (inbound_queue_ != nullptr) {
                inbound_queue_->push(*parsed);
            }
        }
    }
}

bool TcpClient::send_telemetry(const shared::TelemetrySample& sample) {
    shared::TelemetryPayload telemetry;
    telemetry.temperature_c = sample.temperature_c;
    telemetry.humidity_air = sample.humidity_air;
    telemetry.humidity_soil = sample.humidity_soil;
    telemetry.pump_status = sample.pump_status;
    telemetry.motor_status = sample.motor_status;
    telemetry.error_status = sample.error_status;

    const auto telemetry_message = shared::make_message(
        shared::MessageType::Telemetry,
        std::string(shared::constants::kSourceMiljoestyring),
        shared::to_json_obj(telemetry),
        "",
        sample.timestamp_utc
    );

    if (!send_all(shared::encode_json_line(telemetry_message))) {
        shared::log_error("tcp_client", "Failed to send telemetry");
        disconnect();
        return false;
    }
    return true;
}

bool TcpClient::send_all(std::string_view payload) {
    if (!is_connected()) {
        shared::log_error("tcp_client", "send_all called on disconnected socket");
        return false;
    }

    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
#if defined(_WIN32)
        const int sent = send(socket_fd_, data, static_cast<int>(remaining), 0);
#else
        const ssize_t sent = send(socket_fd_, data, remaining, 0);
#endif
        if (sent <= 0) {
            shared::log_error("tcp_client", "send() failed: " + last_socket_error());
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

}  // namespace miljoestyring
