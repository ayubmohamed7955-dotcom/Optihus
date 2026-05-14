#pragma once

#include <atomic>
#include <thread>

#include <gpiod.hpp>

namespace miljoestyring::hw {

class Motor {
public:
    static constexpr int STEPS_PER_REV = 4096;
    static constexpr int STEP_DELAY_US = 2000;

    Motor(int pin1, int pin2, int pin3, int pin4);
    ~Motor();

    void start();
    void stop();

    void open();
    void close();

    void setTemperatureLimits(float minLimit, float maxLimit);
    void setHumidityLimits(float minLimit, float maxLimit);

    float getCurrentTemp() const;
    float getCurrentHumid() const;
    bool isWindowOpen() const;

private:
    void runSteps(int steps);
    void runLoop();

    gpiod::chip chip_;
    gpiod::line_request* request_ {nullptr};
    gpiod::line::offsets pins_;

    std::thread thread_;
    std::atomic<bool> running_ {false};

    std::atomic<float> tempMinLimit_ {12.0F};
    std::atomic<float> tempMaxLimit_ {35.0F};
    std::atomic<float> humidityMinLimit_ {40.0F};
    std::atomic<float> humidityMaxLimit_ {75.0F};
    std::atomic<float> currentTemp_ {0.0F};
    std::atomic<float> currentHumid_ {0.0F};
    std::atomic<bool> shouldOpen_ {false};
    std::atomic<bool> isOpen_ {false};

    static const int SEQ[8][4];
};

}  // namespace miljoestyring::hw
