
#pragma once

#include "datahaandtering/command_dispatcher.hpp"
#include "datahaandtering/config_manager.hpp"

#include <functional>
#include <string>

namespace datahaandtering::web_ui_debug {


bool handle_test_component_request(
    const std::string& request_line,
    ConfigManager& config_manager,
    CommandDispatcher& command_dispatcher,
    const std::function<void(std::string action, bool queued)>& on_control_result,
    std::string& status_line,
    std::string& body
);


bool is_debug_control_path(const std::string& request_line);

}
