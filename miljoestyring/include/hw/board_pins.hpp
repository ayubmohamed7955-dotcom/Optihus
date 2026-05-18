#pragma once

/** GPIO-/bro-pinlayout som partner-zip `main.cpp` (stepper + pumpe). */
namespace miljoestyring::hw_pins {

inline constexpr int MOTOR_PIN1 = 17;
inline constexpr int MOTOR_PIN2 = 18;
inline constexpr int MOTOR_PIN3 = 27;
inline constexpr int MOTOR_PIN4 = 22;

inline constexpr int BOX1_PUMP_PIN = 16;

/**
 * GPIO til DHT22 når luft temp/fugt læses via sysfs IIO (som `Motor::runLoop`).
 * Skal stemme overens med Device Tree-overlay (`dtoverlay=dht11,gpiopin=N`).
 *
 * Partnerens README brugte eksempel med GPIO 17; deres `Motor.cpp` brugte 24 — vi følger koden her.
 */
inline constexpr int DHT22_GPIO_PIN = 24;

} // namespace miljoestyring::hw_pins
