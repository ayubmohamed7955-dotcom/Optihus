#pragma once

#include "shared/types.hpp"

namespace miljoestyring {

class ControlManager {
public:
    void apply_config(const shared::ActiveConfig& config);
    void evaluate(const shared::TelemetrySample& sample);
};

}  // namespace miljoestyring
