
#include "datahaandtering/web_ui_debug.hpp"

#include "datahaandtering/web_ui_http_util.hpp"

namespace datahaandtering::web_ui_debug {

bool is_debug_control_path(const std::string& request_line) {
    return request_line.rfind("POST /api/debug/test-component", 0) == 0
        || request_line.rfind("POST /api/control/test-component", 0) == 0;
}

bool handle_test_component_request(
    const std::string& request_line,
    ConfigManager& config_manager,
    CommandDispatcher& command_dispatcher,
    const std::function<void(std::string action, bool queued)>& on_control_result,
    std::string& status_line,
    std::string& body
) {
    const auto query = web_ui_parse_query(request_line);
    const auto component_it = query.find("component");
    if (component_it == query.end() || component_it->second.empty()) {
        status_line = "HTTP/1.1 400 Bad Request\r\n";
        body = "{\"error\":\"component required (pump or motor)\"}\n";
        return true;
    }
    const auto cfg = config_manager.current_config();
    if (cfg.ventilation_enabled || cfg.watering_enabled) {
        status_line = "HTTP/1.1 409 Conflict\r\n";
        body =
            "{\"error\":\"manual component test requires both ventilation and watering disabled in saved config\"}\n";
        return true;
    }
    std::string action;
    if (component_it->second == "pump") {
        action = "manual_test_pump";
    } else if (component_it->second == "motor") {
        action = "manual_test_motor";
    } else {
        status_line = "HTTP/1.1 400 Bad Request\r\n";
        body = "{\"error\":\"unknown component\"}\n";
        return true;
    }
    const bool queued = command_dispatcher.dispatch_manual_action(action, "pulse");
    on_control_result(action, queued);
    body = std::string("{\"ok\":true,\"action\":\"")
        + action
        + "\",\"queued\":"
        + (queued ? "true" : "false")
        + "}\n";
    return true;
}

}
