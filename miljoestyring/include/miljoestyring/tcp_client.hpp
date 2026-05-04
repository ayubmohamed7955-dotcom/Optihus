#pragma once

#include <string>
#include <string_view>

namespace miljoestyring {

class TcpClient {
public:
    TcpClient(std::string host, int port);
    ~TcpClient();

    bool connect();
    void disconnect();
    bool is_connected() const;

private:
    bool send_all(std::string_view payload);

    std::string host_;
    int port_;
    int socket_fd_ {-1};
};

}  // namespace miljoestyring
