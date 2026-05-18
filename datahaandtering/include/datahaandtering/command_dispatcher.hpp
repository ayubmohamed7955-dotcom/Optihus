#pragma once

#include <string>

#include "datahaandtering/config_manager.hpp"
#include "datahaandtering/database_manager.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"

class CommandDispatcher {
public:
    CommandDispatcher(
        ConfigManager& config_manager,
        DatabaseManager& db_manager,
        ThreadSafeQueue<MessageEnvelope>& outbound_queue
    );

    bool dispatch_current_config();
    bool dispatch_emergency_stop(bool enabled);
    bool dispatch_manual_action(const std::string& action, const std::string& value);

private:
    bool enqueue_command(const CommandPayload& command_payload, const std::string& status);

    ConfigManager& config_manager_;
    DatabaseManager& db_manager_;
    ThreadSafeQueue<MessageEnvelope>& outbound_queue_;
};
