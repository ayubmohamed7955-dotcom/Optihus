#pragma once

#include <string>

#include "datahaandtering/config_manager.hpp"
#include "datahaandtering/database_manager.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"

namespace datahaandtering {

class CommandDispatcher {
public:
    CommandDispatcher(
        ConfigManager& config_manager,
        DatabaseManager& db_manager,
        shared::ThreadSafeQueue<shared::MessageEnvelope>& outbound_queue
    );

    void dispatch_current_config();
    void dispatch_emergency_stop(bool enabled);
    void dispatch_manual_action(const std::string& action, const std::string& value);

private:
    void enqueue_command(const shared::CommandPayload& command_payload, const std::string& status);

    ConfigManager& config_manager_;
    DatabaseManager& db_manager_;
    shared::ThreadSafeQueue<shared::MessageEnvelope>& outbound_queue_;
};

}  // namespace datahaandtering
