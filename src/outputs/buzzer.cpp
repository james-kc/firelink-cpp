#include "outputs/buzzer.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

Buzzer::Buzzer(uint8_t gpio_pin) : pin(gpio_pin) {}

bool Buzzer::begin() {
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

void Buzzer::writeGPIO(const char *value) {
    std::ofstream valueFile("/sys/class/gpio/gpio" + std::to_string(pin) + "/value");
    if (valueFile.is_open()) valueFile << value;
}

void Buzzer::on() { writeGPIO("1"); }

void Buzzer::off() { writeGPIO("0"); }

void Buzzer::beep(int duration_ms) {
    on();
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    off();
}
