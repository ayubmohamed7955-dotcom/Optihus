#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "shared/types.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#endif

namespace miljoestyring {

class TcpClient {
public:
    TcpClient(std::string host, int port);
    ~TcpClient();

    bool connect();
    void disconnect();
    bool is_connected() const;
    bool send_telemetry(const shared::TelemetrySample& sample);

private:
    bool send_all(std::string_view payload);

    std::string host_;
    int port_;
#if defined(_WIN32)
    SOCKET socket_fd_ {INVALID_SOCKET};
    bool winsock_started_ {false};
#else
    int socket_fd_ {-1};
#endif
};

}  // namespace miljoestyring
