#include "miljoestyring/app.hpp"

#include "miljoestyring/control_manager.hpp"
#include "miljoestyring/sensor_manager.hpp"
#include "miljoestyring/tcp_client.hpp"
#include "shared/constants.hpp"
#include "shared/logger.hpp"

namespace miljoestyring {

int App::run() {
    shared::log(shared::LogLevel::Info, "miljoestyring", "Starting field node skeleton");

    TcpClient client(std::string(shared::constants::kDataHostname), shared::constants::kTcpPort);
    if (!client.connect()) {
        shared::log(shared::LogLevel::Warn, "miljoestyring", "Could not connect to server");
    }

    SensorManager sensor_manager;
    ControlManager control_manager;
    const auto sample = sensor_manager.read_sample();
    control_manager.evaluate(sample);

    client.disconnect();
    return 0;
}

}  // namespace miljoestyring
