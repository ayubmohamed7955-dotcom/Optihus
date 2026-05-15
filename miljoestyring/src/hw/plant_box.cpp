#include "hw/plant_box.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/i2c-dev.h>

namespace miljoestyring::hw {

PlantBox::PlantBox(int boxId, int pumpPin)
    : boxId_(boxId),
      pumpPin_(pumpPin),
      file_(-1),
      nextPumpAllowed_(std::chrono::steady_clock::now()) {
    file_ = open("/dev/i2c-1", O_RDWR);
    if (file_ >= 0) {
        ioctl(file_, I2C_SLAVE, 0x48);
    }
}

PlantBox::~PlantBox() {
    stop();
    if (file_ >= 0) {
        close(file_);
        file_ = -1;
    }
}

void PlantBox::start() {
    if (running_.exchange(true)) {
        return;
    }
    if (file_ < 0) {
        running_.store(false);
        return;
    }
    workerThread_ = std::thread(&PlantBox::monitorLoop, this);
}

void PlantBox::stop() {
    running_.store(false);
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    setPumpState(false);
}

void PlantBox::setMinimumThreshold(int value) {
    if (value >= 0 && value <= 100) {
        minimumThreshold_.store(value);
    }
}

void PlantBox::setSkipWaterAtOrAbovePct(int value) {
    if (value >= 0 && value <= 100) {
        skipWaterAtOrAbovePct_.store(value);
    }
}

void PlantBox::setPumpDurationSec(int value) {
    if (value >= 1 && value <= 3600) {
        pumpDurationSec_.store(value);
    }
}

void PlantBox::setHysteresisSec(int value) {
    if (value >= 0 && value <= 864000) {
        hysteresisSec_.store(value);
    }
}

float PlantBox::getCurrentAverageMoisture() const {
    return static_cast<float>(currentAverageMoisture_.load());
}

bool PlantBox::isPumpOn() const {
    return pumpOn_.load();
}

int PlantBox::readADC() {
    char config[3] = {0x01, 0xC3, 0x83};
    if (write(file_, config, 3) != 3) {
        return -1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    char reg[1] = {0x00};
    if (write(file_, reg, 1) != 1) {
        return -1;
    }
    char data[2] = {0};
    if (read(file_, data, 2) != 2) {
        return -1;
    }
    return (data[0] << 8 | data[1]) >> 4;
}

int PlantBox::getMoisturePercentage(int raw_adc, int air_value, int water_value) {
    int percent = ((air_value - raw_adc) * 100) / (air_value - water_value);
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    return percent;
}

void PlantBox::setPumpState(bool turnOn) {
    pumpOn_.store(turnOn);
    const std::string command = turnOn ? ("pinctrl set " + std::to_string(pumpPin_) + " op dh")
                                       : ("pinctrl set " + std::to_string(pumpPin_) + " op dl");
    std::system(command.c_str());
}

void PlantBox::monitorLoop() {
    setPumpState(false);

    while (running_.load()) {
        int total_moisture = 0;
        int valid = 0;

        for (int i = 0; i < 5 && running_.load(); ++i) {
            const int raw = readADC();
            if (raw != -1) {
                total_moisture += getMoisturePercentage(raw, airValue_, waterValue_);
                ++valid;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (valid == 0) {
            continue;
        }

        const int avg = total_moisture / valid;
        currentAverageMoisture_.store(avg);

        const int min_water = minimumThreshold_.load();
        const int max_soil = skipWaterAtOrAbovePct_.load();
        const auto now = std::chrono::steady_clock::now();

        if (avg <= min_water && avg < max_soil && now >= nextPumpAllowed_) {
            const int pump_sec = pumpDurationSec_.load();
            const int hyst_sec = hysteresisSec_.load();
            nextPumpAllowed_ = now + std::chrono::seconds(hyst_sec);
            setPumpState(true);
            std::this_thread::sleep_for(std::chrono::seconds(std::max(1, pump_sec)));
            setPumpState(false);
        }
    }

    setPumpState(false);
}

}  // namespace miljoestyring::hw
