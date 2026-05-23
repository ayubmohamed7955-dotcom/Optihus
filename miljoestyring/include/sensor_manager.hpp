
#pragma once

#include "client_state.hpp"
#include "shared/types.hpp"

class Motor;
class PlantBox;

namespace miljoestyring {

class SensorManager {
public:
    void set_hardware(Motor* motor, PlantBox* plant_box);

    TelemetrySample read_sample(const ClientRuntimeState* runtime) const;

private:
    Motor* motor_ {nullptr};
    PlantBox* plant_box_ {nullptr};
};

}
