#include "app.hpp"

#include "client_state.hpp"
#include "control_manager.hpp"
#include "sensor_manager.hpp"
#include "tcp_client.hpp"

#if OPTIHUS_MILJOESTYRING_HARDWARE
#include "hw/motor.hpp"
#include "hw/plant_box.hpp"
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

namespace miljoestyring {
namespace {

constexpr int kReconnectAttemptsBeforeWifiRecovery = 3;

struct NetworkConfig {
    std::string host {"192.168.4.1"};
    int port {shared::constants::kTcpPort};
};

NetworkConfig load_network_config() {
    NetworkConfig cfg;
    std::ifstream file("miljoestyring_config.json");
    if (!file.is_open()) {
        shared::log_warn(
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
        shared::log_warn(
            "miljoestyring",
            "Kunne ikke parse konfigfil, bruger defaults: " + std::string(ex.what())
        );
    }
    return cfg;
}

#if defined(__linux__)
void attempt_wifi_recovery() {
    shared::log_warn("miljoestyring", "Attempting Wi-Fi recovery (nmcli radio on + rescan)");
    (void)std::system("nmcli radio wifi on >/dev/null 2>&1");
    (void)std::system("nmcli dev wifi rescan >/dev/null 2>&1");
}
#endif

}  // namespace

int App::run() {
    shared::log_info("miljoestyring", "Starting field node");

    const auto network_cfg = load_network_config();
    shared::log_info(
        "miljoestyring",
        "Server target: " + network_cfg.host + ":" + std::to_string(network_cfg.port)
    );

#if OPTIHUS_MILJOESTYRING_HARDWARE
    miljoestyring::hw::Motor motor(17, 18, 27, 22);
    miljoestyring::hw::PlantBox plant_box(1, 16);
    motor.start();
    plant_box.start();
#endif

    SensorManager sensor_manager;
#if OPTIHUS_MILJOESTYRING_HARDWARE
    sensor_manager.set_hardware(&motor, &plant_box);
#else
    sensor_manager.set_hardware(nullptr, nullptr);
#endif

    ClientRuntimeState runtime;
    ControlManager control_manager;
    shared::ThreadSafeQueue<shared::MessageEnvelope> inbound;

    TcpClient client(network_cfg.host, network_cfg.port);

    auto reconnect = [&]() {
        client.disconnect();
        int attempts = 0;
        while (!client.connect()) {
            ++attempts;
            shared::log_warn(
                "miljoestyring",
                "Could not connect to server. Retrying in "
                    + std::to_string(shared::constants::kReconnectDelaySec)
                    + " seconds"
            );
#if defined(__linux__)
            if (attempts % kReconnectAttemptsBeforeWifiRecovery == 0) {
                attempt_wifi_recovery();
            }
#endif
            std::this_thread::sleep_for(std::chrono::seconds(shared::constants::kReconnectDelaySec));
        }
        if (attempts > 0) {
            shared::log_info("miljoestyring", "Connection re-established after retry loop");
        }
        client.start_reader(inbound);
    };

    reconnect();

    while (true) {
        if (!client.is_connected() || !client.is_reader_running()) {
            shared::log_warn("miljoestyring", "Connection lost. Reconnecting...");
            reconnect();
        }

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

        const auto sample = sensor_manager.read_sample(&runtime);
        if (!client.send_telemetry(sample)) {
            shared::log_warn("miljoestyring", "Telemetry send failed. Reconnecting...");
            reconnect();
            continue;
        }

        const int sleep_sec = runtime.telemetry_interval_sec.load();
        const int bounded_sleep = std::max(1, sleep_sec);
        for (int waited = 0; waited < bounded_sleep; ++waited) {
            if (!client.is_connected() || !client.is_reader_running()) {
                shared::log_warn("miljoestyring", "Connection lost during wait. Reconnecting...");
                reconnect();
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

}  // namespace miljoestyring
