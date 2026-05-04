#pragma once

#include <string>

namespace miljoestyring {

class ActuatorManager {
public:
    void set_pump(bool on);
    void set_motor_state(const std::string& state);
};

}  // namespace miljoestyring
