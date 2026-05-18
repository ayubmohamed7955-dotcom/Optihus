#include "datahaandtering/telemetry_processor.hpp"
#include "datahaandtering/web_ui_server.hpp"

#include "shared/constants.hpp"
#include "shared/logger.hpp"

TelemetryProcessor::TelemetryProcessor(
    DatabaseManager& db_manager,
    ThreadSafeQueue<MessageEnvelope>& telemetry_queue,
    WebUiServer& web_ui_server
)
    : db_manager_(db_manager),
      telemetry_queue_(telemetry_queue),
      web_ui_server_(web_ui_server) {}

TelemetryProcessor::~TelemetryProcessor() {
    stop();
}

void TelemetryProcessor::start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_thread_ = std::thread(&TelemetryProcessor::worker_loop, this);
}

void TelemetryProcessor::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    telemetry_queue_.shutdown();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void TelemetryProcessor::worker_loop() {
    while (running_) {
        const auto next = telemetry_queue_.wait_pop();
        if (!next.has_value()) {
            if (!running_) {
                break;
            }
            continue;
        }
        process_telemetry_message(*next);
    }
}

void TelemetryProcessor::process_telemetry_message(const MessageEnvelope& message) {
    const auto telemetry = telemetry_from_json(message.data);
    if (!telemetry.has_value()) {
        log_warn("telemetry_processor", "Skipping invalid telemetry payload");
        return;
    }

    TelemetrySample sample;
    sample.timestamp_utc = message.ts_utc;
    sample.temperature_c = telemetry->temperature_c;
    sample.humidity_air = telemetry->humidity_air;
    sample.humidity_soil = telemetry->humidity_soil;
    sample.pump_status = telemetry->pump_status;
    sample.motor_status = telemetry->motor_status;
    sample.error_status = telemetry->error_status;

    if (!telemetry->field_live.empty()) {
        web_ui_server_.note_field_live(telemetry->field_live.dump(), message.ts_utc);
    }

    if (!db_manager_.insert_telemetry(
            message.ts_utc,
            sample,
            std::string(kSourceMiljoestyring)
        )) {
        log_error("telemetry_processor", "Failed to insert telemetry row");
    }
}

