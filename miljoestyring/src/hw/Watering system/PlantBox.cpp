// PlantBox.cpp

#include "hw/Watering system/PlantBox.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
auto pumpPauseTime = std::chrono::system_clock::now();
}

PlantBox::PlantBox(int boxId, int pumpPin)
    : boxId(boxId),
      pumpPin(pumpPin),
      file(-1),
      running(false),
      minimumThreshold(100),
      pauseBetweenWateringSec(86400),
      airValue(1600),
      waterValue(400) {
    file = open("/dev/i2c-1", O_RDWR);

    if (file >= 0) {
        ioctl(file, I2C_SLAVE, 0x48);
    }
}

PlantBox::~PlantBox() {
    stop();

    if (file >= 0) {
        close(file);
    }
}

void PlantBox::start() {
    if (!running && file >= 0) {
        running = true;
        workerThread = std::thread(&PlantBox::monitorLoop, this);
    }
}

void PlantBox::stop() {
    running = false;

    if (workerThread.joinable()) {
        workerThread.join();
    }

    setPumpState(false);
}

void PlantBox::setMinimumThreshold(int value) {
    if (value >= 0 && value <= 100) {
        minimumThreshold = value;
    }
}

void PlantBox::setPauseBetweenWateringSec(int value) {
    if (value < 0) {
        pauseBetweenWateringSec = 0;
        return;
    }
    if (value > 86400) {
        pauseBetweenWateringSec = 86400;
        return;
    }
    pauseBetweenWateringSec = value;
}

int PlantBox::readADC() {
    char config[3] = {0x01, 0xC3, 0x83};

    if (write(file, config, 3) != 3) {
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    char reg[1] = {0x00};

    if (write(file, reg, 1) != 1) {
        return -1;
    }

    char data[2] = {0};

    if (read(file, data, 2) != 2) {
        return -1;
    }

    return (data[0] << 8 | data[1]) >> 4;
}

int PlantBox::getMoisturePercentage(int raw_adc) {
    int percent =
        ((airValue - raw_adc) * 100) /
        (airValue - waterValue);

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
    std::string command;

    if (turnOn) {
        command =
            "pinctrl set " +
            std::to_string(pumpPin) +
            " op dh";
    } else {
        command =
            "pinctrl set " +
            std::to_string(pumpPin) +
            " op dl";
    }

    system(command.c_str());
}

void PlantBox::setActuationEnabled(bool enabled) {
    actuation_enabled_.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        setPumpState(false);
    }
}

void PlantBox::monitorLoop() {
    setPumpState(false);

    while (running) {

        int totalMoisture = 0;
        int totalADC = 0;
        int validMeasurements = 0;

        // 5 målinger over 10 sekunder
        for (int i = 0; i < 5 && running; i++) {

            int raw_adc = readADC();

            if (raw_adc != -1) {

                int moisture =
                    getMoisturePercentage(raw_adc);

                totalMoisture += moisture;
                totalADC += raw_adc;

                validMeasurements++;
            }

            std::this_thread::sleep_for(
                std::chrono::seconds(1)
            );
        }

        if (validMeasurements > 0) {

            int averageMoisture =
                totalMoisture / validMeasurements;

            int averageADC =
                totalADC / validMeasurements;

            printf(
                "Box %d | Avg moisture: %d%% | Avg ADC: %d\n",
                boxId,
                averageMoisture,
                averageADC
            );

            currentAverageMoisture = averageMoisture;

            // Hvis jorden er for tør
            if (actuation_enabled_.load(std::memory_order_relaxed) &&
                averageMoisture <= minimumThreshold && pumpPauseTime < std::chrono::system_clock::now()) {
                pumpPauseTime = std::chrono::system_clock::now()
                    + std::chrono::seconds(pauseBetweenWateringSec);
                printf(
                    "Box %d | Pump ON\n",
                    boxId
                );

                setPumpState(true);

                // Pumpen kører 1 sekund
                std::this_thread::sleep_for(
                    std::chrono::seconds(1)
                );

                setPumpState(false);

                printf(
                    "Box %d | Pump OFF\n",
                    boxId
                );

                printf(
                    "Box %d | Waiting %d seconds\n",
                    boxId,
                    pauseBetweenWateringSec
                );

                // Pause efter vanding
                // std::this_thread::sleep_for(
                //     std::chrono::seconds(10)
                //);
            }
        }
    }

    setPumpState(false);
}

float PlantBox::getCurrentAverageMoisture() {
    return static_cast<float>(currentAverageMoisture.load());
}
