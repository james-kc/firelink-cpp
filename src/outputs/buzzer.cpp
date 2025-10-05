#include "outputs/buzzer.h"

Buzzer::Buzzer(unsigned int pin) : gpio_pin(pin), chip(nullptr), line(nullptr) {}

bool Buzzer::begin() {
    chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) { std::cerr << "Failed to open gpiochip0\n"; return false; }

    line = gpiod_chip_get_line(chip, gpio_pin);
    if (!line) { std::cerr << "Failed to get line\n"; return false; }

    if (gpiod_line_request_output(line, "firelink-buzzer", 0) < 0) {
        std::cerr << "Failed to request output\n";
        return false;
    }

    return true;
}

void Buzzer::on() {
    if (line) gpiod_line_set_value(line, 1);
}

void Buzzer::off() {
    if (line) gpiod_line_set_value(line, 0);
}

void Buzzer::beep(int duration_ms) {
    on();
    usleep(duration_ms * 1000);
    off();
}

Buzzer::~Buzzer() {
    if (line) gpiod_line_release(line);
    if (chip) gpiod_chip_close(chip);
}
