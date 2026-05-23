
#ifndef DHT22_SENSOR_HPP
#define DHT22_SENSOR_HPP

class DHT22 {
private:
    int pin;
    int byte1, byte2, byte3, byte4;

public:
    DHT22(int gpio_pin);

    float getTemperature();
    float getHumidity();

    bool update();
};

#endif
