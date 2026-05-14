#include "hw/motor.hpp"

#include "hw/DHT22.hpp"

#include <chrono>
#include <thread>
#include <unistd.h>

namespace miljoestyring::hw {

namespace {
constexpr int kDht22Pin = 24;
constexpr int kSensorIntervalSec = 2;
}  // namespace

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
      pins_({static_cast<unsigned int>(pin1),
             static_cast<unsigned int>(pin2),
             static_cast<unsigned int>(pin3),
             static_cast<unsigned int>(pin4)}) {
    request_ = new gpiod::line_request(
        chip_.prepare_request()
            .set_consumer("optihus_stepper")
            .add_line_settings(
                pins_,
                gpiod::line_settings().set_direction(gpiod::line::direction::OUTPUT)
            )
            .do_request()
    );
}

Motor::~Motor() {
    stop();
    if (request_ != nullptr) {
        for (auto pin : pins_) {
            request_->set_value(pin, gpiod::line::value::INACTIVE);
        }
        delete request_;
        request_ = nullptr;
    }
}

void Motor::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread(&Motor::runLoop, this);
}

void Motor::stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Motor::setTemperatureLimits(float minLimit, float maxLimit) {
    tempMinLimit_.store(minLimit);
    tempMaxLimit_.store(maxLimit);
}

void Motor::setHumidityLimits(float minLimit, float maxLimit) {
    humidityMinLimit_.store(minLimit);
    humidityMaxLimit_.store(maxLimit);
}

float Motor::getCurrentTemp() const {
    return currentTemp_.load();
}

float Motor::getCurrentHumid() const {
    return currentHumid_.load();
}

bool Motor::isWindowOpen() const {
    return isOpen_.load();
}

void Motor::runLoop() {
    DHT22 sensor(kDht22Pin);

    while (running_.load()) {
        if (!sensor.update()) {
            std::this_thread::sleep_for(std::chrono::seconds(kSensorIntervalSec));
            continue;
        }

        const float temp = sensor.getTemperature();
        const float humidity = sensor.getHumidity();
        currentTemp_.store(temp);
        currentHumid_.store(humidity);

        const float t_min = tempMinLimit_.load();
        const float t_max = tempMaxLimit_.load();
        const float h_min = humidityMinLimit_.load();
        const float h_max = humidityMaxLimit_.load();

        if (humidity < h_min || temp < t_min) {
            shouldOpen_.store(false);
        }
        if (humidity > h_max || temp > t_max) {
            shouldOpen_.store(true);
        }

        if (shouldOpen_.load() && !isOpen_.load()) {
            isOpen_.store(true);
            open();
        }
        if (!shouldOpen_.load() && isOpen_.load()) {
            isOpen_.store(false);
            close();
        }

        std::this_thread::sleep_for(std::chrono::seconds(kSensorIntervalSec));
    }
}

void Motor::runSteps(int steps) {
    const int total_steps = (steps < 0) ? -steps : steps;
    const bool forward = (steps >= 0);

    for (int i = 0; i < total_steps; ++i) {
        const int seq_index = forward ? (i % 8) : (7 - (i % 8));
        for (int j = 0; j < 4; ++j) {
            const auto val = SEQ[seq_index][j] ? gpiod::line::value::ACTIVE : gpiod::line::value::INACTIVE;
            request_->set_value(pins_[j], val);
        }
        usleep(static_cast<useconds_t>(STEP_DELAY_US));
    }

    for (auto pin : pins_) {
        request_->set_value(pin, gpiod::line::value::INACTIVE);
    }
}

void Motor::open() {
    runSteps(STEPS_PER_REV);
}

void Motor::close() {
    runSteps(-STEPS_PER_REV);
}

}  // namespace miljoestyring::hw
