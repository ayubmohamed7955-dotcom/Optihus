#include "datahaandtering/app.hpp"
#include "datahaandtering/command_dispatcher.hpp"
#include "datahaandtering/config_manager.hpp"
#include "datahaandtering/database_manager.hpp"
#include "datahaandtering/tcp_server.hpp"
#include "datahaandtering/telemetry_processor.hpp"
#include "datahaandtering/web_ui_server.hpp"

#include <csignal>
#include <string>
#include <string_view>

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/message.hpp"
#include "shared/paths.hpp"
#include "shared/thread_safe_queue.hpp"

static App* g_app = nullptr;

static void handle_signal(int /*signal_number*/) {
    if (g_app != nullptr) {
        g_app->request_stop();
    }
}

int main(int argc, char** argv) {
    const char* db_cli = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_cli = argv[++i];
        }
    }

    const std::string db_path = optihus::resolve_db_path(db_cli);
    if (!optihus::ensure_parent_directories(db_path)) {
        log_error("main", "Could not create database directory for " + db_path);
        return 1;
    }

    DatabaseManager db_manager{db_path};
    if (!db_manager.open()) {
        log_error("main", "Could not open database at " + db_path);
        return 1;
    }
    log_info("main", "Database: " + db_path);

    if (!db_manager.initialize_schema("sql/init.sql")) {
        log_warn("main", "Could not run schema file. Assuming schema already exists.");
    }
    if (!db_manager.apply_schema_migrations()) {
        log_error("main", "Could not apply database migrations");
        return 1;
    }

    ConfigManager config_manager(db_manager);
    if (!config_manager.load_active_config()) {
        log_warn("main", "No active config loaded; applying factory defaults once");
        if (!db_manager.apply_factory_defaults()) {
            log_error("main", "Could not apply factory defaults");
            return 1;
        }
        if (!config_manager.load_active_config()) {
            log_error("main", "Could not load active config after factory defaults");
            return 1;
        }
    }

    (void)db_manager.prune_telemetry_older_than_days(kTelemetryRetentionDays);

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
