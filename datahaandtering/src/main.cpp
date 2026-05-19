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
#include <optional>
#include <string>
#include <string_view>

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/message.hpp"
#include "shared/paths.hpp"
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
    const char* db_cli = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--tui") {
            tui_mode = true;
        } else if (arg == "--db" && i + 1 < argc) {
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

    if (tui_mode) {
        TuiManager tui(db_manager, config_manager);
        return tui.run();
    }
    const auto reset_profiles_to_defaults = [&db_manager]() {
        for (const auto& profile : db_manager.list_profiles()) {
            if (!profile.is_default) {
                db_manager.delete_profile(profile.name);
            }
        }
        const auto now = now_utc_iso8601();
        const std::array<Profile, 3> defaults {
            Profile {"tomato", 55.0, 75.0, 35.0, 60.0, 18.0, 26.0, 60, true},
            Profile {"cucumber", 60.0, 85.0, 45.0, 70.0, 20.0, 28.0, 60, true},
            Profile {"chili", 50.0, 70.0, 30.0, 55.0, 22.0, 30.0, 60, true},
        };
        for (const auto& profile : defaults) {
            db_manager.upsert_profile(profile, now);
        }
    };

    const auto reset_plant_boxes_to_defaults = [&db_manager]() {
        for (const auto& box : db_manager.list_plant_boxes()) {
            if (!box.is_default) {
                db_manager.delete_plant_box(box.id);
            }
        }

        const auto now = now_utc_iso8601();
        const auto boxes = db_manager.list_plant_boxes();
        std::optional<PlantBoxConfig> existing_default;
        for (const auto& existing : boxes) {
            if (existing.is_default) {
                existing_default = existing;
                break;
            }
        }
        PlantBoxConfig box;
        if (existing_default.has_value()) {
            box.id = existing_default->id;
        }
        box.label = kDefaultPlantBoxLabel;
        box.slot = 1;
        box.is_active = false;
        box.is_default = true;
        box.min_soil_pct = kDefaultPlantMinSoilPct;
        box.max_soil_pct = 100.0;
        box.pump_duration_sec = 1;
        box.hysteresis_sec = kDefaultPlantHysteresisSec;
        db_manager.upsert_plant_box(box, now);
    };

    reset_profiles_to_defaults();
    reset_plant_boxes_to_defaults();
    config_manager.load_active_config();
    config_manager.set_profile_name("tomato");
    config_manager.set_interval_sec(60);
    config_manager.set_emergency_stop(false);
    config_manager.save_active_config();

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
