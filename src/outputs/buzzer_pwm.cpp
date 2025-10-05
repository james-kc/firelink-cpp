#include "outputs/buzzer_pwm.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

BuzzerPWM::BuzzerPWM(uint8_t gpio_pin) : pin(gpio_pin), tone_active(false) {}

bool BuzzerPWM::begin() {
    std::ofstream exportFile("/sys/class/gpio/export");
    if (!exportFile.is_open()) {
        std::cerr << "Failed to open /sys/class/gpio/export. Are you root?" << std::endl;
        return false;
    }
    exportFile << (int)pin;
    exportFile.close();

    std::ofstream dirFile("/sys/class/gpio/gpio" + std::to_string(pin) + "/direction");
    if (!dirFile.is_open()) {
        std::cerr << "Failed to set GPIO direction." << std::endl;
        return false;
    }
    dirFile << "out";
    dirFile.close();
    return true;
}

void BuzzerPWM::writeGPIO(const char *value) {
    std::ofstream valueFile("/sys/class/gpio/gpio" + std::to_string(pin) + "/value");
    if (valueFile.is_open()) valueFile << value;
}

void BuzzerPWM::on() { writeGPIO("1"); }
void BuzzerPWM::off() { writeGPIO("0"); }

void BuzzerPWM::beep(int duration_ms) {
    on();
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    off();
}

void BuzzerPWM::tone(int frequency, int duration_ms) {
    if (tone_active) return; // ignore if already playing

    tone_active = true;
    std::thread([=]() {
        int period_us = 1000000 / frequency; // microseconds per cycle
        int half_period = period_us / 2;
        int elapsed = 0;

        while (tone_active && elapsed < duration_ms * 1000) {
            writeGPIO("1");
            std::this_thread::sleep_for(std::chrono::microseconds(half_period));
            writeGPIO("0");
            std::this_thread::sleep_for(std::chrono::microseconds(half_period));
            elapsed += period_us;
        }
        writeGPIO("0");
        tone_active = false;
    }).detach();
}

void BuzzerPWM::noTone() {
    tone_active = false; // stops the detached thread
}
