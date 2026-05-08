#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "datahaandtering/command_dispatcher.hpp"
#include "datahaandtering/tcp_server.hpp"

namespace datahaandtering {

class WebUiServer {
public:
    WebUiServer(int port, TcpServer& tcp_server, CommandDispatcher& command_dispatcher);
    ~WebUiServer();

    WebUiServer(const WebUiServer&) = delete;
    WebUiServer& operator=(const WebUiServer&) = delete;

    bool start();
    void stop();
    void note_client_hello(const std::string& timestamp_utc);

private:
    void accept_loop();
    void handle_client(int client_socket);
    bool send_all(int client_socket, const std::string& payload);
    bool handle_control_request(
        const std::string& request_line,
        std::string& status_line,
        std::string& body,
        std::string& content_type
    );
    void note_control_result(std::string action, bool queued);
    std::string build_status_json() const;
    static std::string build_index_html();

    int port_;
    TcpServer& tcp_server_;
    CommandDispatcher& command_dispatcher_;
    int listen_socket_ {-1};
    std::atomic<bool> running_ {false};
    std::thread accept_thread_;

    std::atomic<std::uint64_t> hello_count_ {0};
    mutable std::mutex status_mutex_;
    std::string last_hello_ts_ {"never"};
    std::string last_control_action_ {"none"};
    std::string last_control_ts_ {"never"};
    bool last_control_queued_ {false};
};

}  // namespace datahaandtering
