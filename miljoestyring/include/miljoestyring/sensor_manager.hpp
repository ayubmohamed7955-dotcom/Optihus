#pragma once

#include "shared/types.hpp"

namespace miljoestyring {

class SensorManager {
public:
    shared::TelemetrySample read_sample() const;
};

}  // namespace miljoestyring
