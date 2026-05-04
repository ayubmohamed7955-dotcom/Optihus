#include "miljoestyring/tcp_client.hpp"

#include "shared/logger.hpp"

namespace miljoestyring {

TcpClient::TcpClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

bool TcpClient::connect() {
    shared::log(shared::LogLevel::Info, "tcp_client",
                "TCP client skeleton connect (reconnect logic added in later phase)");
    return !host_.empty() && port_ > 0;
}

void TcpClient::disconnect() {
    shared::log(shared::LogLevel::Info, "tcp_client", "TCP client skeleton disconnect");
}

}  // namespace miljoestyring
