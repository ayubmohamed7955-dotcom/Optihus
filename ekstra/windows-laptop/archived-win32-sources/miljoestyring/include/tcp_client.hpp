#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"
#include "shared/types.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#endif

class TcpClient {
public:
    TcpClient(std::string host, int port);
    ~TcpClient();

    bool connect();
    void disconnect();
    bool is_connected() const;
    bool is_reader_running() const;

    void start_reader(ThreadSafeQueue<MessageEnvelope>& inbound);
    void stop_reader();

    bool send_telemetry(const TelemetrySample& sample);

private:
    void read_loop();
    bool send_all(std::string_view payload);

    std::string host_;
    int port_;

#if defined(_WIN32)
    SOCKET socket_fd_ {INVALID_SOCKET};
    bool winsock_started_ {false};
#else
    int socket_fd_ {-1};
#endif

    std::thread reader_thread_;
    std::atomic<bool> reader_run_ {false};
    ThreadSafeQueue<MessageEnvelope>* inbound_queue_ {nullptr};
    std::string read_buffer_;
};
