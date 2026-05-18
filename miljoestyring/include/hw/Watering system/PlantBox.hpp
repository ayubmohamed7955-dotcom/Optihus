// PlantBox.h

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

    int airValue;
    int waterValue;

    int readADC();
    int getMoisturePercentage(int raw_adc);
    void setPumpState(bool turnOn);
    void monitorLoop();

    std::atomic<int> currentAverageMoisture;
    /** Optihus: seneste pumpe-tilstand til telemetry (ikke i oprindelig zip-header). */
    std::atomic<bool> pumpOn_{false};
    std::atomic<bool> actuation_enabled_{false};

public:
    PlantBox(int boxId, int pumpPin);
    ~PlantBox();

    void start();
    void stop();

    void setMinimumThreshold(int value);
    float getCurrentAverageMoisture();
    bool isPumpOn() const { return pumpOn_.load(); }

    /** false ved levering: jordfugt opdateres stadig; pumpe først når Optihus sætter true efter server-konfiguration. */
    void setActuationEnabled(bool enabled);
    bool isActuationEnabled() const { return actuation_enabled_.load(std::memory_order_relaxed); }
    bool isRunning() const { return running.load(std::memory_order_relaxed); }
};

#endif
