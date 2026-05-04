#include "miljoestyring/app.hpp"

#include "miljoestyring/control_manager.hpp"
#include "miljoestyring/sensor_manager.hpp"
#include "miljoestyring/tcp_client.hpp"
#include "shared/constants.hpp"
#include "shared/logger.hpp"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

namespace miljoestyring {
namespace {

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

}  // namespace

int App::run() {
    shared::log_info("miljoestyring", "Starting field node");

    const auto network_cfg = load_network_config();
    shared::log_info(
        "miljoestyring",
        "Server target: " + network_cfg.host + ":" + std::to_string(network_cfg.port)
    );

    TcpClient client(network_cfg.host, network_cfg.port);
    if (!client.connect()) {
        shared::log_error("miljoestyring", "Could not connect to server");
        return 1;
    }

    SensorManager sensor_manager;
    ControlManager control_manager;
    shared::log_info("miljoestyring", "TCP startup hello sent; running loop");

    while (true) {
        const auto sample = sensor_manager.read_sample();
        control_manager.evaluate(sample);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

}  // namespace miljoestyring
