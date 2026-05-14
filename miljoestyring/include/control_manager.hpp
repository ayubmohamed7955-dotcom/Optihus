#pragma once

#include "client_state.hpp"
#include "shared/message.hpp"
#include "shared/types.hpp"

namespace miljoestyring::hw {
class Motor;
class PlantBox;
}  // namespace miljoestyring::hw

namespace miljoestyring {

class ControlManager {
public:
    void apply_inbound_message(
        const shared::MessageEnvelope& message,
        ClientRuntimeState& runtime,
        miljoestyring::hw::Motor* motor,
        miljoestyring::hw::PlantBox* plant
    );
};

}  // namespace miljoestyring
