#include "hw/Ventilation system/DHT22.hpp"
#include <iostream>
#include <fstream>
#include <string>

DHT22::DHT22(int gpio_pin)
    : pin(gpio_pin), byte1(0), byte2(0), byte3(0), byte4(0)
{

}

float DHT22::getTemperature()
{
    return ((byte3 << 8) | byte4) / 10.0; //Udregning af temperatur
}

float DHT22::getHumidity()
{
    return ((byte1 << 8) | byte2) / 10.0; //Udregning af luftfugtighed
}

bool DHT22::update() // Opdaterer sensor data ved at læse fra filer
{
    //Læser data fra filerne i Linux systemet, som indeholder data fra DHT22 sensoren
    std::ifstream humidityFile("/sys/bus/iio/devices/iio:device0/in_humidityrelative_input");
    std::ifstream temperatureFile("/sys/bus/iio/devices/iio:device0/in_temp_input");

    // Tjekker om filerne blev åbnet 
    if (!humidityFile.is_open() || !temperatureFile.is_open()) 
    {
        std::cerr << "Filerne kunne ikke åbnes" << std::endl;
        return false;
    }

    // Gemmer data som strings
    std::string humidityString, temperatureString;
    std::getline(humidityFile, humidityString);
    std::getline(temperatureFile, temperatureString);

    try 
    {   
        // Konverterer strings til integers og deler med 100 for at få de korrekte værdier
        int humidityRaw = std::stoi(humidityString) / 100;
        int temperatureRaw = std::stoi(temperatureString) / 100;

        // Opdaterer byte variablerne med de nye værdier
        byte1 = (humidityRaw >> 8) & 0xFF;
        byte2 = humidityRaw & 0xFF;
        byte3 = (temperatureRaw >> 8) & 0xFF;
        byte4 = temperatureRaw & 0xFF;
        
        return true;
    } 

    //Fejl ved opdatering fanges her
    catch (...)
    {
        return false; 
    }
}
