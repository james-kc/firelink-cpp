#include "outputs/buzzer_pwm.h"

BuzzerPWM::BuzzerPWM(unsigned int pin) : gpio_pin(pin), chip(nullptr), line(nullptr) {}

bool BuzzerPWM::begin() {
    chip = gpiod_chip_open_by_name("gpiochip0");
    if (!chip) { std::cerr << "Failed to open gpiochip0\n"; return false; }

    line = gpiod_chip_get_line(chip, gpio_pin);
    if (!line) { std::cerr << "Failed to get line\n"; return false; }

    if (gpiod_line_request_output(line, "firelink-buzzer-pwm", 0) < 0) {
        std::cerr << "Failed to request output\n";
        return false;
    }

    return true;
}

void BuzzerPWM::beep(int duration_ms) {
    gpiod_line_set_value(line, 1);
    usleep(duration_ms * 1000);
    gpiod_line_set_value(line, 0);
}

void BuzzerPWM::tone(int frequency, int duration_ms) {
    if (frequency <= 0) return;

    int period_us = 1000000 / frequency;
    int cycles = duration_ms * 1000 / period_us;

    for (int i = 0; i < cycles; ++i) {
        gpiod_line_set_value(line, 1);
        usleep(period_us / 2);
        gpiod_line_set_value(line, 0);
        usleep(period_us / 2);
    }
}

void BuzzerPWM::playMelody(int notes[], int durations[]) {
    size_t len = std::min(notes.size(), durations.size());
    for (size_t i = 0; i < len; ++i) {
        tone(notes[i], durations[i]);
        usleep(durations[i] * 50);
    }
}

BuzzerPWM::~BuzzerPWM() {
    if (line) gpiod_line_release(line);
    if (chip) gpiod_chip_close(chip);
}
