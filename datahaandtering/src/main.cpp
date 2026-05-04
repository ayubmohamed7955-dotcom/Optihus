#include "datahaandtering/app.hpp"
#include "datahaandtering/command_dispatcher.hpp"
#include "datahaandtering/config_manager.hpp"
#include "datahaandtering/database_manager.hpp"
#include "datahaandtering/tcp_server.hpp"
#include "datahaandtering/telemetry_processor.hpp"
#include "datahaandtering/tui_manager.hpp"

#include <array>
#include <csignal>
#include <string>
#include <string_view>

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"
#include "shared/time_utils.hpp"

namespace {
datahaandtering::App* g_app = nullptr;

void handle_signal(int /*signal_number*/) {
    if (g_app != nullptr) {
        g_app->request_stop();
    }
}
}  // namespace

int main(int argc, char** argv) {
    bool tui_mode = false;
    if (argc > 1) {
        const std::string_view arg1 = argv[1];
        tui_mode = (arg1 == "--tui");
    }

    datahaandtering::DatabaseManager db_manager(std::string(shared::constants::kDbPath));
    if (!db_manager.open()) {
        shared::log_error("main", "Could not open database");
        return 1;
    }

    // Composition root: all modules are created and wired here.
    datahaandtering::ConfigManager config_manager(db_manager);

    if (tui_mode) {
        datahaandtering::TuiManager tui(db_manager, config_manager);
        return tui.run();
    }

    if (!db_manager.initialize_schema("sql/init.sql")) {
        shared::log_warn("main", "Could not run schema file. Assuming schema already exists.");
    }
    const auto ensure_default_profiles = [&db_manager]() {
        const auto now = shared::now_utc_iso8601();
        const std::array<shared::Profile, 3> defaults {
            shared::Profile {"tomato", "Tomato", 55.0, 75.0, 35.0, 60.0, 900, true},
            shared::Profile {"cucumber", "Cucumber", 60.0, 85.0, 45.0, 70.0, 900, true},
            shared::Profile {"chili", "Chili", 50.0, 70.0, 30.0, 55.0, 900, true},
        };
        for (const auto& profile : defaults) {
            db_manager.upsert_profile(profile, now);
        }
    };
    ensure_default_profiles();
    config_manager.load_active_config();

    shared::ThreadSafeQueue<shared::MessageEnvelope> inbound_queue;
    shared::ThreadSafeQueue<shared::MessageEnvelope> outbound_queue;
    shared::ThreadSafeQueue<shared::MessageEnvelope> telemetry_queue;

    datahaandtering::TcpServer tcp_server(shared::constants::kTcpPort, inbound_queue, outbound_queue);
    datahaandtering::TelemetryProcessor telemetry_processor(db_manager, telemetry_queue);
    datahaandtering::CommandDispatcher command_dispatcher(config_manager, db_manager, outbound_queue);
    datahaandtering::App app(
        tcp_server,
        telemetry_processor,
        command_dispatcher,
        inbound_queue,
        telemetry_queue
    );

    g_app = &app;
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    const int code = app.run();
    g_app = nullptr;

    db_manager.close();
    return code;
}
