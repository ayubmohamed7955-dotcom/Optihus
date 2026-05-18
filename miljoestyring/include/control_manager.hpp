#pragma once

#include "client_state.hpp"
#include "shared/message.hpp"

class Motor;
class PlantBox;

namespace miljoestyring {

class ControlManager {
public:
    void apply_inbound_message(
        const MessageEnvelope& message,
        ClientRuntimeState& runtime,
        Motor* motor,
        PlantBox* plant_box
    );
};

}
