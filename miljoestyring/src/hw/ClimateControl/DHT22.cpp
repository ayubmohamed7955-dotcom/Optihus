
#include "hw/ClimateControl/DHT22.hpp"
#include <iostream>
#include <fstream>
#include <string>

DHT22::DHT22(int gpio_pin)
    : pin(gpio_pin), byte1(0), byte2(0), byte3(0), byte4(0)
{

}

float DHT22::getTemperature()
{
    return ((byte3 << 8) | byte4) / 10.0;
}

float DHT22::getHumidity()
{
    return ((byte1 << 8) | byte2) / 10.0;
}

bool DHT22::update()
{

    std::ifstream humidityFile("/sys/bus/iio/devices/iio:device0/in_humidityrelative_input");
    std::ifstream temperatureFile("/sys/bus/iio/devices/iio:device0/in_temp_input");


    if (!humidityFile.is_open() || !temperatureFile.is_open())
    {
        std::cerr << "Filerne kunne ikke åbnes" << std::endl;
        return false;
    }


    std::string humidityString, temperatureString;
    std::getline(humidityFile, humidityString);
    std::getline(temperatureFile, temperatureString);

    try
    {

        int humidityRaw = std::stoi(humidityString) / 100;
        int temperatureRaw = std::stoi(temperatureString) / 100;


        byte1 = (humidityRaw >> 8) & 0xFF;
        byte2 = humidityRaw & 0xFF;
        byte3 = (temperatureRaw >> 8) & 0xFF;
        byte4 = temperatureRaw & 0xFF;

        return true;
    }


    catch (...)
    {
        return false;
    }
}
