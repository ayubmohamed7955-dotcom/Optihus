#include "datahaandtering/app.hpp"
#include "datahaandtering/command_dispatcher.hpp"
#include "datahaandtering/config_manager.hpp"
#include "datahaandtering/database_manager.hpp"
#include "datahaandtering/tcp_server.hpp"
#include "datahaandtering/telemetry_processor.hpp"
#include "datahaandtering/tui_manager.hpp"
#include "datahaandtering/web_ui_server.hpp"

#include <array>
#include <csignal>
#include <string>
#include <string_view>

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/message.hpp"
#include "shared/thread_safe_queue.hpp"
#include "shared/time_utils.hpp"

static App* g_app = nullptr;

static void handle_signal(int /*signal_number*/) {
    if (g_app != nullptr) {
        g_app->request_stop();
    }
}

int main(int argc, char** argv) {
    bool tui_mode = false;
    if (argc > 1) {
        const std::string_view arg1 = argv[1];
        tui_mode = (arg1 == "--tui");
    }

    DatabaseManager db_manager{std::string{kDbPath}};
    if (!db_manager.open()) {
        log_error("main", "Could not open database");
        return 1;
    }

    ConfigManager config_manager(db_manager);

    if (tui_mode) {
        TuiManager tui(db_manager, config_manager);
        return tui.run();
    }

    if (!db_manager.initialize_schema("sql/init.sql")) {
        log_warn("main", "Could not run schema file. Assuming schema already exists.");
    }
    const auto ensure_default_profiles = [&db_manager]() {
        const auto now = now_utc_iso8601();
        const std::array<Profile, 3> defaults {
            Profile {"tomato", 55.0, 75.0, 35.0, 60.0, 60, true},
            Profile {"cucumber", 60.0, 85.0, 45.0, 70.0, 60, true},
            Profile {"chili", 50.0, 70.0, 30.0, 55.0, 60, true},
        };
        for (const auto& profile : defaults) {
            db_manager.upsert_profile(profile, now);
        }
    };
    ensure_default_profiles();
    config_manager.load_active_config();

    ThreadSafeQueue<MessageEnvelope> inbound_queue;
    ThreadSafeQueue<MessageEnvelope> outbound_queue;
    ThreadSafeQueue<MessageEnvelope> telemetry_queue;

    TcpServer tcp_server(kTcpPort, inbound_queue, outbound_queue);
    CommandDispatcher command_dispatcher(config_manager, db_manager, outbound_queue);
    WebUiServer web_ui_server(
        kHttpUiPort,
        tcp_server,
        command_dispatcher,
        config_manager,
        db_manager
    );
    TelemetryProcessor telemetry_processor(db_manager, telemetry_queue, web_ui_server);
    App app(
        tcp_server,
        web_ui_server,
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
