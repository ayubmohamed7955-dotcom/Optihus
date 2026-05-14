#include "hw/DHT22.hpp"

#include <fstream>
#include <string>

namespace miljoestyring::hw {

DHT22::DHT22(int gpio_pin) : pin_(gpio_pin) {}

bool DHT22::update() {
    std::ifstream humidity_file("/sys/bus/iio/devices/iio:device0/in_humidityrelative_input");
    std::ifstream temperature_file("/sys/bus/iio/devices/iio:device0/in_temp_input");

    if (!humidity_file.is_open() || !temperature_file.is_open()) {
        return false;
    }

    std::string humidity_string;
    std::string temperature_string;
    std::getline(humidity_file, humidity_string);
    std::getline(temperature_file, temperature_string);

    try {
        const int humidity_raw = std::stoi(humidity_string) / 100;
        const int temperature_raw = std::stoi(temperature_string) / 100;
        byte1_ = (humidity_raw >> 8) & 0xFF;
        byte2_ = humidity_raw & 0xFF;
        byte3_ = (temperature_raw >> 8) & 0xFF;
        byte4_ = temperature_raw & 0xFF;
        return true;
    } catch (...) {
        return false;
    }
}

float DHT22::getTemperature() const {
    return static_cast<float>(((byte3_ << 8) | byte4_)) / 10.0F;
}

float DHT22::getHumidity() const {
    return static_cast<float>(((byte1_ << 8) | byte2_)) / 10.0F;
}

}  // namespace miljoestyring::hw
