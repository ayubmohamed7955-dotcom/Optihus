#pragma once

#include <atomic>
#include <chrono>
#include <thread>

namespace miljoestyring::hw {

class PlantBox {
public:
    explicit PlantBox(int boxId, int pumpPin);
    ~PlantBox();

    void start();
    void stop();

    void setMinimumThreshold(int value);
    void setSkipWaterAtOrAbovePct(int value);
    void setPumpDurationSec(int value);
    void setHysteresisSec(int value);

    float getCurrentAverageMoisture() const;
    bool isPumpOn() const;

private:
    int readADC();
    static int getMoisturePercentage(int raw_adc, int air_value, int water_value);
    void setPumpState(bool turnOn);
    void monitorLoop();

    int boxId_;
    int pumpPin_;
    int file_;

    std::atomic<bool> running_ {false};
    std::thread workerThread_;

    std::atomic<int> minimumThreshold_ {35};
    std::atomic<int> skipWaterAtOrAbovePct_ {100};
    std::atomic<int> pumpDurationSec_ {1};
    std::atomic<int> hysteresisSec_ {600};
    int airValue_ {1600};
    int waterValue_ {400};

    std::atomic<int> currentAverageMoisture_ {0};
    std::atomic<bool> pumpOn_ {false};

    std::chrono::steady_clock::time_point nextPumpAllowed_ {};
};

}  // namespace miljoestyring::hw
