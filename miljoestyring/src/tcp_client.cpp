
#include "tcp_client.hpp"

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/message.hpp"
#include "shared/message_codec.hpp"
#include "shared/time_utils.hpp"

#include <nlohmann/json.hpp>


static std::string last_socket_error() {
    return std::strerror(errno);
}

TcpClient::TcpClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

TcpClient::~TcpClient() {
    disconnect();
}

void TcpClient::stop_reader() {
    reader_run_.store(false);
    if (reader_thread_.joinable()) {
        if (socket_fd_ >= 0) {
            shutdown(socket_fd_, SHUT_RDWR);
        }
        reader_thread_.join();
    }
}

bool TcpClient::connect() {


    if (is_connected()) {
        return true;
    }
    if (host_.empty() || port_ <= 0) {
        log_error("tcp_client", "Invalid server host/port");
        return false;
    }

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (!is_connected()) {
        log_error("tcp_client", "socket() failed: " + last_socket_error());
        return false;
    }

    sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (inet_pton(AF_INET, host_.c_str(), &server_addr.sin_addr) != 1) {
        log_error("tcp_client", "Invalid IPv4 address: " + host_);
        disconnect();
        return false;
    }

    if (::connect(socket_fd_, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0) {
        log_error("tcp_client", "connect() failed: " + last_socket_error());
        disconnect();
        return false;
    }

    HelloPayload hello_payload;
    hello_payload.node = std::string(kMiljoeHostname);
    hello_payload.version = "0.1.0";

    const auto hello_message = make_message(
        MessageType::Hello,
        std::string(kSourceMiljoestyring),
        to_json_obj(hello_payload),
        "",
        now_utc_iso8601()
    );

    if (!send_all(encode_json_line(hello_message))) {
        log_error("tcp_client", "Failed to send hello message");
        disconnect();
        return false;
    }

    log_info("tcp_client", "Connected and sent hello to " + host_ + ":" + std::to_string(port_));
    return true;
}

void TcpClient::disconnect() {
    stop_reader();
    read_buffer_.clear();
    if (is_connected()) {
        shutdown(socket_fd_, SHUT_RDWR);
        close(socket_fd_);
        socket_fd_ = -1;
        log_info("tcp_client", "Disconnected");
    }
}

bool TcpClient::is_connected() const {
    return socket_fd_ >= 0;
}

bool TcpClient::is_reader_running() const {
    return reader_run_.load();
}

void TcpClient::start_reader(ThreadSafeQueue<MessageEnvelope>& inbound) {
    stop_reader();
    inbound_queue_ = &inbound;
    read_buffer_.clear();
    reader_run_.store(true);
    reader_thread_ = std::thread(&TcpClient::read_loop, this);
}

void TcpClient::read_loop() {
    char chunk[1024];

    while (reader_run_.load() && is_connected()) {
        const ssize_t n = recv(socket_fd_, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            if (n == 0) {
                log_info("tcp_client", "Server closed connection (recv=0)");
            } else if (reader_run_.load()) {
                log_warn("tcp_client", "recv failed: " + last_socket_error());
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

            const auto parsed = decode_json_line(line);
            if (!parsed.has_value()) {
                log_warn("tcp_client", "Dropped invalid JSON line");
                continue;
            }
            if (inbound_queue_ != nullptr) {
                inbound_queue_->push(*parsed);
            }
        }
    }
}

bool TcpClient::send_telemetry(const TelemetrySample& sample) {


    TelemetryPayload telemetry;
    telemetry.temperature_c = sample.temperature_c;
    telemetry.humidity_air = sample.humidity_air;
    telemetry.humidity_soil = sample.humidity_soil;
    telemetry.pump_status = sample.pump_status;
    telemetry.motor_status = sample.motor_status;
    telemetry.error_status = sample.error_status;
    if (!sample.field_live_json.empty()) {
        try {
            const auto parsed = nlohmann::json::parse(sample.field_live_json);
            if (parsed.is_object() && !parsed.empty()) {
                telemetry.field_live = parsed;
            }
        } catch (...) {
        }
    }

    const auto telemetry_message = make_message(
        MessageType::Telemetry,
        std::string(kSourceMiljoestyring),
        to_json_obj(telemetry),
        "",
        sample.timestamp_utc
    );

    if (!send_all(encode_json_line(telemetry_message))) {
        log_error("tcp_client", "Failed to send telemetry");
        disconnect();
        return false;
    }
    return true;
}

bool TcpClient::send_all(std::string_view payload) {
    if (!is_connected()) {
        log_error("tcp_client", "send_all called on disconnected socket");
        return false;
    }

    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t sent = send(socket_fd_, data, remaining, 0);
        if (sent <= 0) {
            log_error("tcp_client", "send() failed: " + last_socket_error());
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}
