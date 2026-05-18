#pragma once

#include <gpiod.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <stdexcept>

class Motor
{
public:
    static constexpr float TEMP_MIN     = -10.0f;
    static constexpr float TEMP_MAX     =  50.0f;
    static constexpr float HUMIDITY_MIN =   0.0f;
    static constexpr float HUMIDITY_MAX = 100.0f;

    Motor(int pin1, int pin2, int pin3, int pin4);
    ~Motor();

    void open();
    void close();

    // Starter en thread der løbende læser sensor og styrer vindue
    void start();
    void stop();

    void setTemperatureLimits(float minLimit, float maxLimit);
    void setHumidityLimits(float minLimit, float maxLimit);

    float getCurrentTemp() const;
    float getCurrentHumid() const;

    bool isWindowOpen() const;

    /** false ved levering/opstart: loop læser stadig sensor; vinduesmotor kører først når Optihus sætter true efter server-konfiguration. */
    void setActuationEnabled(bool enabled);
    bool isActuationEnabled() const { return actuation_enabled_.load(std::memory_order_relaxed); }
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }

private:
    gpiod::chip chip_;
    gpiod::line_request* request_;
    gpiod::line::offsets pins_;

    std::thread thread_;
    std::atomic<bool> running_;

    std::atomic<float> tempMinLimit_{18.0f};
    std::atomic<float> tempMaxLimit_{20.0f};
    std::atomic<float> humidityMinLimit_{30.0f};
    std::atomic<float> humidityMaxLimit_{40.0f};
    std::atomic<float> currentTemp_{0.0f};
    std::atomic<float> currentHumid_{0.0f};

    bool isOpen_ = false;
    bool shouldOpen = false;

    std::atomic<bool> actuation_enabled_{false};

    static constexpr int STEPS_PER_REV = 4096;
    static constexpr int STEP_DELAY_US = 2000;
    static const int SEQ[8][4];

    void runSteps(int steps);
    void runLoop();
};
