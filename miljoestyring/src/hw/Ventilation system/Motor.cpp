#include "hw/Ventilation system/Motor.hpp"

#include "hw/board_pins.hpp"
#include "hw/Ventilation system/DHT22.hpp"
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <stdexcept>

using miljoestyring::hw_pins::DHT22_GPIO_PIN;
static constexpr int SENSOR_INTERVAL_SEC = 2;

const int Motor::SEQ[8][4] = {
    {1, 0, 0, 0},
    {1, 1, 0, 0},
    {0, 1, 0, 0},
    {0, 1, 1, 0},
    {0, 0, 1, 0},
    {0, 0, 1, 1},
    {0, 0, 0, 1},
    {1, 0, 0, 1},
};

Motor::Motor(int pin1, int pin2, int pin3, int pin4)
    : chip_("/dev/gpiochip0"),
      request_(nullptr),
      pins_({(unsigned int)pin1, (unsigned int)pin2,
             (unsigned int)pin3, (unsigned int)pin4}),
      running_(false) {
    request_ = new gpiod::line_request(
        chip_.prepare_request()
            .set_consumer("stepper")
            .add_line_settings(
                pins_,
                gpiod::line_settings().set_direction(gpiod::line::direction::OUTPUT)
            )
            .do_request()
    );
}

Motor::~Motor() {
    stop();
    if (request_) {
        for (auto pin : pins_) {
            request_->set_value(pin, gpiod::line::value::INACTIVE);
        }
        delete request_;
    }
}

void Motor::start() {
    if (running_) {
        return;
    }

    running_ = true;
    thread_ = std::thread(&Motor::runLoop, this);
}

void Motor::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Motor::setTemperatureLimits(float minLimit, float maxLimit) {
    tempMinLimit_ = minLimit;
    tempMaxLimit_ = maxLimit;
}

void Motor::setHumidityLimits(float minLimit, float maxLimit) {
    humidityMinLimit_ = minLimit;
    humidityMaxLimit_ = maxLimit;
}

float Motor::getCurrentTemp() const {
    return currentTemp_.load();
}

float Motor::getCurrentHumid() const {
    return currentHumid_.load();
}

bool Motor::isWindowOpen() const {
    return isOpen_;
}

void Motor::setActuationEnabled(bool enabled) {
    actuation_enabled_.store(enabled, std::memory_order_relaxed);
}

void Motor::runLoop() {
    DHT22 sensor(DHT22_GPIO_PIN);

    while (running_) {
        if (!sensor.update()) {
            std::cerr << "Sensor fejl: kunne ikke opdatere DHT22-data" << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(SENSOR_INTERVAL_SEC));
            continue;
        }

        float temp = sensor.getTemperature();
        float humidity = sensor.getHumidity();

        currentTemp_.store(temp);
        currentHumid_.store(humidity);

        std::cout << "Temp: " << temp << " C, Fugt: " << humidity << " %" << std::endl;

        if (actuation_enabled_.load(std::memory_order_relaxed)) {
            const float tempMin = tempMinLimit_.load();
            const float tempMax = tempMaxLimit_.load();
            const float humidityMin = humidityMinLimit_.load();
            const float humidityMax = humidityMaxLimit_.load();

            std::cout << temp << " temp\n";
            std::cout << tempMax << " tempMax\n";
            std::cout << shouldOpen << "\n";

            if (humidity < humidityMin || temp < tempMin) {
                std::cout << "shouldclose\n";
                shouldOpen = false;
            }
            //overwrite with opening if both conditions are true, to prioritize opening for high temp/humidity
            if (humidity > humidityMax || temp > tempMax) {
                std::cout << "shouldopen\n";
                shouldOpen = true;
            }
            if (shouldOpen && !isOpen_) {
                std::cout << "opening\n";
                isOpen_ = true;
                open();
            }
            if (!shouldOpen && isOpen_) {
                std::cout << "closing\n";
                isOpen_ = false;
                close();
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(SENSOR_INTERVAL_SEC));
    }
}

void Motor::runSteps(int steps) {
    int totalSteps = (steps < 0) ? -steps : steps;
    bool forward = (steps >= 0);

    for (int i = 0; i < totalSteps; i++) {
        int seqIndex = forward ? (i % 8) : (7 - (i % 8));

        for (int j = 0; j < 4; j++) {
            auto val = SEQ[seqIndex][j] ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE;
            request_->set_value(pins_[j], val);
        }

        usleep(STEP_DELAY_US);
    }

    for (auto pin : pins_) {
        request_->set_value(pin, gpiod::line::value::INACTIVE);
    }
}

void Motor::open() {
    std::cout << "Åbner vindue..." << std::endl;
    runSteps(STEPS_PER_REV);
}

void Motor::close() {
    std::cout << "Lukker vindue..." << std::endl;
    runSteps(-STEPS_PER_REV);
}
