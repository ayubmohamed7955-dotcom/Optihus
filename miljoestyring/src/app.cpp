#include "app.hpp"

#include "client_state.hpp"
#include "control_manager.hpp"
#include "sensor_manager.hpp"
#include "set_config_store.hpp"
#include "tcp_client.hpp"

#include "shared/message.hpp"

#if OPTIHUS_MILJOESTYRING_HARDWARE
#include "hw/board_pins.hpp"
#include "hw/Ventilation system/Motor.hpp"
#include "hw/Watering system/PlantBox.hpp"
#endif

#include "shared/constants.hpp"
#include "shared/logger.hpp"
#include "shared/thread_safe_queue.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

class Motor;
class PlantBox;

using namespace miljoestyring;

static constexpr int kReconnectAttemptsBeforeWifiRecovery = 3;

struct NetworkConfig {
    std::string host {"192.168.4.1"};
    int port {kTcpPort};
};

static NetworkConfig load_network_config() {
    NetworkConfig cfg;
    std::ifstream file("miljoestyring_config.json");
    if (!file.is_open()) {
        log_warn(
            "miljoestyring",
            "Konfigfil ikke fundet (miljoestyring_config.json), bruger defaults"
        );
        return cfg;
    }
    try {
        const auto j = nlohmann::json::parse(file);
        cfg.host = j.value("server_host", cfg.host);
        cfg.port = j.value("server_port", cfg.port);
    } catch (const std::exception& ex) {
        log_warn(
            "miljoestyring",
            "Kunne ikke parse konfigfil, bruger defaults: " + std::string(ex.what())
        );
    }
    return cfg;
}

#if defined(__linux__)
static void attempt_wifi_recovery() {
    log_warn("miljoestyring", "Attempting Wi-Fi recovery (nmcli radio on + rescan)");
    (void)std::system("nmcli radio wifi on >/dev/null 2>&1");
    (void)std::system("nmcli dev wifi rescan >/dev/null 2>&1");
}
#endif

/** Snapshot til web UI «field_node» via telemetri (`field_live`). */
static std::string build_field_live_json(
    const TcpClient& client,
    const ClientRuntimeState& runtime,
    Motor* motor,
    PlantBox* plant_box
) {
    nlohmann::json j;
    j["node"] = "miljoestyring";
#if OPTIHUS_MILJOESTYRING_HARDWARE
    j["hardware_build"] = "gpio_i2c";
#else
    j["hardware_build"] = "stub_no_gpio";
#endif
    j["tcp"]["connected"] = client.is_connected();
    j["tcp"]["reader_running"] = client.is_reader_running();
    j["runtime"]["server_config_received"] = runtime.server_config_received.load();
    j["runtime"]["emergency_halt"] = runtime.emergency_halt.load();

    auto& comps = j["components"] = nlohmann::json::array();

    const bool tcp_live = client.is_connected() && client.is_reader_running();
    comps.push_back(nlohmann::json {
        {"id", "tcp_link"},
        {"role", "forbindelse_mod_datahaandtering"},
        {"live", tcp_live},
        {"detail", tcp_live ? "socket_og_laese_traad_ok" : "frakoblet_eller_laeser_stoppet"}
    });

#if OPTIHUS_MILJOESTYRING_HARDWARE
    if (motor != nullptr) {
        const bool vent_run = motor->isRunning();
        const bool vent_act = motor->isActuationEnabled();
        comps.push_back(nlohmann::json {
            {"id", "ventilation"},
            {"role", "dht22_lufttemp_fugt_og_stepper_vindue"},
            {"sensor_loop_live", vent_run},
            {"actuation_enabled", vent_act},
            {"mode", vent_run ? (vent_act ? "regulerer_vindue" : "kun_sensning") : "loop_stoppet"},
            {"window_reported", motor->isWindowOpen() ? "open" : "closed"}
        });
    }
    if (plant_box != nullptr) {
        const bool box_run = plant_box->isRunning();
        const bool box_act = plant_box->isActuationEnabled();
        comps.push_back(nlohmann::json {
            {"id", "plant_box"},
            {"role", "adc_jordfugt_og_pumpe"},
            {"sensor_loop_live", box_run},
            {"actuation_enabled", box_act},
            {"mode",
             box_run ? (box_act ? "overvaagning_og_vanding_tilladt" : "kun_overvaagning") : "loop_stoppet"},
            {"pump_output", plant_box->isPumpOn() ? "on" : "off"}
        });
    }
#else
    (void)motor;
    (void)plant_box;
    comps.push_back(nlohmann::json {
        {"id", "felt_hardware"},
        {"role", "gpio_i2c"},
        {"live", false},
        {"detail", "OPTIHUS_MILJOESTYRING_HARDWARE=OFF"}
    });
#endif

    return j.dump();
}


int App::run() {
    log_info("miljoestyring", "Starting field node");

    const auto network_cfg = load_network_config();
    log_info(
        "miljoestyring",
        "Server target: " + network_cfg.host + ":" + std::to_string(network_cfg.port)
    );

#if OPTIHUS_MILJOESTYRING_HARDWARE
    Motor motor(
        miljoestyring::hw_pins::MOTOR_PIN1,
        miljoestyring::hw_pins::MOTOR_PIN2,
        miljoestyring::hw_pins::MOTOR_PIN3,
        miljoestyring::hw_pins::MOTOR_PIN4
    );
    PlantBox plant_box(1, miljoestyring::hw_pins::BOX1_PUMP_PIN);
    motor.start();
    plant_box.start();
    log_info(
        "miljoestyring",
        "Sensor-tråde startet ved boot (telemetri); motor og pumpe aktiveres først efter "
        "web-/server-konfiguration (set_config)"
    );
#endif

    SensorManager sensor_manager;
#if OPTIHUS_MILJOESTYRING_HARDWARE
    sensor_manager.set_hardware(&motor, &plant_box);
#else
    sensor_manager.set_hardware(nullptr, nullptr);
#endif

    ClientRuntimeState runtime;
    ControlManager control_manager;
    ThreadSafeQueue<MessageEnvelope> inbound;

    TcpClient client(network_cfg.host, network_cfg.port);

    if (const auto replay_params = load_field_config_for_replay()) {
        MessageEnvelope replay;
        replay.type = MessageType::Command;
        replay.data["action"] = "set_config";
        replay.data["params"] = *replay_params;
#if OPTIHUS_MILJOESTYRING_HARDWARE
        control_manager.apply_inbound_message(replay, runtime, &motor, &plant_box);
#else
        control_manager.apply_inbound_message(replay, runtime, nullptr, nullptr);
#endif
        log_info(
            "miljoestyring",
            "Genindsatte persistert feltkonfiguration fra disk (sidste set_config / nødstop-overlay)"
        );
    }

    auto reconnect = [&]() {
        client.disconnect();
        int attempts = 0;
        while (!client.connect()) {
            ++attempts;
            log_warn(
                "miljoestyring",
                "Could not connect to server. Retrying in "
                    + std::to_string(kReconnectDelaySec)
                    + " seconds"
            );
#if defined(__linux__)
            if (attempts % kReconnectAttemptsBeforeWifiRecovery == 0) {
                attempt_wifi_recovery();
            }
#endif
            std::this_thread::sleep_for(std::chrono::seconds(kReconnectDelaySec));
        }
        if (attempts > 0) {
            log_info("miljoestyring", "Connection re-established after retry loop");
        }
        client.start_reader(inbound);
    };

    auto process_inbound = [&]() {
        for (;;) {
            const auto msg = inbound.try_pop();
            if (!msg.has_value()) {
                break;
            }
#if OPTIHUS_MILJOESTYRING_HARDWARE
            control_manager.apply_inbound_message(*msg, runtime, &motor, &plant_box);
#else
            control_manager.apply_inbound_message(*msg, runtime, nullptr, nullptr);
#endif
        }
    };

    reconnect();

    while (true) {
        if (!client.is_connected() || !client.is_reader_running()) {
            log_warn("miljoestyring", "Connection lost. Reconnecting...");
            reconnect();
        }

        process_inbound();

        TelemetrySample enriched = sensor_manager.read_sample(&runtime);
#if OPTIHUS_MILJOESTYRING_HARDWARE
        enriched.field_live_json =
            build_field_live_json(client, runtime, &motor, &plant_box);
#else
        enriched.field_live_json = build_field_live_json(client, runtime, nullptr, nullptr);
#endif
        if (!client.send_telemetry(enriched)) {
            log_warn("miljoestyring", "Telemetry send failed. Reconnecting...");
            reconnect();
            continue;
        }

        const int sleep_sec = runtime.telemetry_interval_sec.load();
        const int bounded_sleep = std::max(1, sleep_sec);
        constexpr int kInboundPollStepMs = 200;
        const int total_wait_ms = bounded_sleep * 1000;
        for (int waited_ms = 0; waited_ms < total_wait_ms; waited_ms += kInboundPollStepMs) {
            process_inbound();
            if (!client.is_connected() || !client.is_reader_running()) {
                log_warn("miljoestyring", "Connection lost during wait. Reconnecting...");
                reconnect();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kInboundPollStepMs));
        }
    }
}

