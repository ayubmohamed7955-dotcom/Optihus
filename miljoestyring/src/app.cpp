#include "miljoestyring/app.hpp"

#include "miljoestyring/control_manager.hpp"
#include "miljoestyring/sensor_manager.hpp"
#include "miljoestyring/tcp_client.hpp"
#include "shared/constants.hpp"
#include "shared/logger.hpp"

#include <chrono>
#include <thread>

namespace miljoestyring {

int App::run() {
    shared::log_info("miljoestyring", "Starting field node");

    TcpClient client("192.168.0.144", shared::constants::kTcpPort);
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
