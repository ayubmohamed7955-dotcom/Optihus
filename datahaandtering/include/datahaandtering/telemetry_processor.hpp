#pragma once

#include <atomic>
#include <thread>

#include "datahaandtering/database_manager.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"

namespace datahaandtering {

class TelemetryProcessor {
public:
    TelemetryProcessor(
        DatabaseManager& db_manager,
        shared::ThreadSafeQueue<shared::MessageEnvelope>& telemetry_queue
    );
    ~TelemetryProcessor();

    void start();
    void stop();

private:
    void worker_loop();
    void process_telemetry_message(const shared::MessageEnvelope& message);

    DatabaseManager& db_manager_;
    shared::ThreadSafeQueue<shared::MessageEnvelope>& telemetry_queue_;
    std::atomic<bool> running_ {false};
    std::thread worker_thread_;
};

}  // namespace datahaandtering
