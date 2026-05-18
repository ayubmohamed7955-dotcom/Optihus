#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"

class TcpServer {
public:
    TcpServer(
        int port,
        ThreadSafeQueue<MessageEnvelope>& inbound_queue,
        ThreadSafeQueue<MessageEnvelope>& outbound_queue
    );
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool start();
    void stop();
    bool is_client_connected() const;

private:
    void accept_loop();
    void start_session(int client_socket);
    void stop_session();
    void stop_session_locked();
    void read_loop();
    void write_loop();

    int port_;
    int listen_socket_ {-1};
    int client_socket_ {-1};
    ThreadSafeQueue<MessageEnvelope>& inbound_queue_;
    ThreadSafeQueue<MessageEnvelope>& outbound_queue_;
    std::atomic<bool> running_ {false};
    std::atomic<bool> session_running_ {false};
    std::thread accept_thread_;
    std::mutex session_mutex_;
    std::thread read_thread_;
    std::thread write_thread_;
};
