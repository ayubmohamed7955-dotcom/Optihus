#pragma once

#include <atomic>
#include <thread>

#include "datahaandtering/database_manager.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"

class WebUiServer;

class TelemetryProcessor {
public:
    TelemetryProcessor(
        DatabaseManager& db_manager,
        ThreadSafeQueue<MessageEnvelope>& telemetry_queue,
        WebUiServer& web_ui_server
    );
    ~TelemetryProcessor();

    void start();
    void stop();

private:
    void worker_loop();
    void process_telemetry_message(const MessageEnvelope& message);

    DatabaseManager& db_manager_;
    ThreadSafeQueue<MessageEnvelope>& telemetry_queue_;
    WebUiServer& web_ui_server_;
    std::atomic<bool> running_ {false};
    std::thread worker_thread_;
};
