


#ifndef PLANT_BOX_H
#define PLANT_BOX_H

#include <atomic>
#include <thread>

class PlantBox {
private:
    int boxId;
    int pumpPin;
    int file;

    std::atomic<bool> running;
    std::thread workerThread;

    int minimumThreshold;
    int pauseBetweenWateringSec;

    int airValue;
    int waterValue;

    int readADC();
    int getMoisturePercentage(int raw_adc);
    void setPumpState(bool turnOn);
    void monitorLoop();

    std::atomic<int> currentAverageMoisture;

    std::atomic<bool> pumpOn_{false};
    std::atomic<bool> actuation_enabled_{false};

public:
    PlantBox(int boxId, int pumpPin);
    ~PlantBox();

    void start();
    void stop();

    void setMinimumThreshold(int value);

    void setPauseBetweenWateringSec(int value);
    float getCurrentAverageMoisture();
    bool isPumpOn() const { return pumpOn_.load(); }
    void manualPumpPulseSec(int seconds);


    void setActuationEnabled(bool enabled);
    bool isActuationEnabled() const { return actuation_enabled_.load(std::memory_order_relaxed); }
    bool isRunning() const { return running.load(std::memory_order_relaxed); }
};

#endif
