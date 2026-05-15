#include "datahaandtering/app.hpp"

#include "shared/constants.hpp"
#include "shared/logger.hpp"

#include <sstream>
#include <utility>

namespace datahaandtering {

App::App(
    TcpServer& tcp_server,
    WebUiServer& web_ui_server,
    TelemetryProcessor& telemetry_processor,
    CommandDispatcher& command_dispatcher,
    shared::ThreadSafeQueue<shared::MessageEnvelope>& inbound_queue,
    shared::ThreadSafeQueue<shared::MessageEnvelope>& telemetry_queue
)
    : tcp_server_(tcp_server),
      web_ui_server_(web_ui_server),
      telemetry_processor_(telemetry_processor),
      command_dispatcher_(command_dispatcher),
      inbound_queue_(inbound_queue),
      telemetry_queue_(telemetry_queue) {}

int App::run() {
    if (!tcp_server_.start()) {
        shared::log_error("datahaandtering", "Failed to start TCP server");
        return 1;
    }
    if (!web_ui_server_.start()) {
        shared::log_error("datahaandtering", "Failed to start Web ui server");
        tcp_server_.stop();
        return 1;
    }
    telemetry_processor_.start();

    {
        std::ostringstream oss;
        oss << "Backend running. Web ui on port "
            << shared::constants::kHttpUiPort
            << " (open http://<this-pi-ip>:"
            << shared::constants::kHttpUiPort
            << " in a browser; AP example http://192.168.4.1:"
            << shared::constants::kHttpUiPort
            << "). TUI optional via --tui";
        shared::log_info("datahaandtering", oss.str());
    }

    while (running_) {
        const auto next = inbound_queue_.wait_pop();
        if (!next.has_value()) {
            if (!running_) {
                break;
            }
            continue;
        }
        handle_message(*next);
    }

    telemetry_processor_.stop();
    web_ui_server_.stop();
    tcp_server_.stop();
    return 0;
}

void App::request_stop() {
    running_ = false;
    inbound_queue_.shutdown();
    telemetry_queue_.shutdown();
}

void App::handle_message(const shared::MessageEnvelope& message) {
    switch (message.type) {
        case shared::MessageType::Hello:
            shared::log_info("app", "Received hello. Sending active config.");
            web_ui_server_.note_client_hello(message.ts_utc);
            command_dispatcher_.dispatch_current_config();
            break;
        case shared::MessageType::Telemetry:
            telemetry_queue_.push(message);
            break;
        case shared::MessageType::Ack:
            shared::log_info("app", "Received ack from client");
            break;
        default:
            shared::log_warn("app", "Unhandled message type");
            break;
    }
}

}  // namespace datahaandtering
