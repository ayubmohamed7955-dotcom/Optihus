
#pragma once

#include <atomic>

#include "datahaandtering/command_dispatcher.hpp"
#include "datahaandtering/tcp_server.hpp"
#include "datahaandtering/telemetry_processor.hpp"
#include "datahaandtering/web_ui_server.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"

class App {
public:
    App(
        TcpServer& tcp_server,
        WebUiServer& web_ui_server,
        TelemetryProcessor& telemetry_processor,
        CommandDispatcher& command_dispatcher,
        ThreadSafeQueue<MessageEnvelope>& inbound_queue,
        ThreadSafeQueue<MessageEnvelope>& telemetry_queue
    );

    int run();
    void request_stop();

private:
    void handle_message(const MessageEnvelope& message);

    TcpServer& tcp_server_;
    WebUiServer& web_ui_server_;
    TelemetryProcessor& telemetry_processor_;
    CommandDispatcher& command_dispatcher_;
    ThreadSafeQueue<MessageEnvelope>& inbound_queue_;
    ThreadSafeQueue<MessageEnvelope>& telemetry_queue_;
    std::atomic<bool> running_ {true};
};
