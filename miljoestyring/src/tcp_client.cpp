#include "miljoestyring/tcp_client.hpp"

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
#include "shared/message_codec.hpp"
#include "shared/time_utils.hpp"

namespace miljoestyring {

TcpClient::TcpClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

TcpClient::~TcpClient() {
    disconnect();
}

bool TcpClient::connect() {
    if (is_connected()) {
        return true;
    }
    if (host_.empty() || port_ <= 0) {
        shared::log_error("tcp_client", "Invalid server host/port");
        return false;
    }

    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        shared::log_error("tcp_client", "socket() failed: " + std::string(std::strerror(errno)));
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
        shared::log_error("tcp_client", "connect() failed: " + std::string(std::strerror(errno)));
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
    if (socket_fd_ >= 0) {
        shutdown(socket_fd_, SHUT_RDWR);
        close(socket_fd_);
        socket_fd_ = -1;
        shared::log_info("tcp_client", "Disconnected");
    }
}

bool TcpClient::is_connected() const {
    return socket_fd_ >= 0;
}

bool TcpClient::send_all(std::string_view payload) {
    if (!is_connected()) {
        shared::log_error("tcp_client", "send_all called on disconnected socket");
        return false;
    }

    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t sent = send(socket_fd_, data, remaining, 0);
        if (sent <= 0) {
            shared::log_error("tcp_client", "send() failed: " + std::string(std::strerror(errno)));
            return false;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
    return true;
}

}  // namespace miljoestyring
