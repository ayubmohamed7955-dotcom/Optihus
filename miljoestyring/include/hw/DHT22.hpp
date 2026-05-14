#pragma once

namespace miljoestyring::hw {

/// Reads humidity and temperature values.
class DHT22 {
public:
    explicit DHT22(int gpio_pin);

    float getTemperature() const;
    float getHumidity() const;
    bool update();

private:
    int pin_;
    int byte1_ {0};
    int byte2_ {0};
    int byte3_ {0};
    int byte4_ {0};
};

}  // namespace miljoestyring::hw
