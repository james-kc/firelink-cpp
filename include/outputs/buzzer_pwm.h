#ifndef BUZZER_PWM_H
#define BUZZER_PWM_H

#include <gpiod.h>
#include <unistd.h>
#include <iostream>
#include <vector>

class BuzzerPWM {
public:
    BuzzerPWM(unsigned int pin = 4);
    bool begin();
    void beep(int duration_ms);
    void tone(int frequency, int duration_ms);
    void playMelody(const std::vector<int> &notes, const std::vector<int> &durations);

    ~BuzzerPWM();

private:
    unsigned int gpio_pin;
    gpiod_chip *chip;
    gpiod_line *line;
};

#endif
