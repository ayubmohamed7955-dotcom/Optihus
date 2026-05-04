#pragma once

#include <string>

namespace miljoestyring {

class TcpClient {
public:
    TcpClient(std::string host, int port);
    bool connect();
    void disconnect();

private:
    std::string host_;
    int port_;
};

}  // namespace miljoestyring
