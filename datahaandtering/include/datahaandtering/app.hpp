#pragma once

#include <atomic>

#include "datahaandtering/command_dispatcher.hpp"
#include "datahaandtering/tcp_server.hpp"
#include "datahaandtering/telemetry_processor.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"

namespace datahaandtering {

class App {
public:
    App(
        TcpServer& tcp_server,
        TelemetryProcessor& telemetry_processor,
        CommandDispatcher& command_dispatcher,
        shared::ThreadSafeQueue<shared::MessageEnvelope>& inbound_queue,
        shared::ThreadSafeQueue<shared::MessageEnvelope>& telemetry_queue
    );

    int run();
    void request_stop();

private:
    void handle_message(const shared::MessageEnvelope& message);

    TcpServer& tcp_server_;
    TelemetryProcessor& telemetry_processor_;
    CommandDispatcher& command_dispatcher_;
    shared::ThreadSafeQueue<shared::MessageEnvelope>& inbound_queue_;
    shared::ThreadSafeQueue<shared::MessageEnvelope>& telemetry_queue_;
    std::atomic<bool> running_ {true};
};

}  // namespace datahaandtering
