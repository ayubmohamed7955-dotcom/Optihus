#pragma once

#include "client_state.hpp"
#include "shared/types.hpp"

namespace miljoestyring::hw {
class Motor;
class PlantBox;
}  // namespace miljoestyring::hw

namespace miljoestyring {

class SensorManager {
public:
    void set_hardware(miljoestyring::hw::Motor* motor, miljoestyring::hw::PlantBox* plant);

    shared::TelemetrySample read_sample(const ClientRuntimeState* runtime) const;

private:
    miljoestyring::hw::Motor* motor_ {nullptr};
    miljoestyring::hw::PlantBox* plant_ {nullptr};
};

}  // namespace miljoestyring
