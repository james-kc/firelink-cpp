#ifndef BUZZER_H
#define BUZZER_H

#include <gpiod.h>
#include <unistd.h>
#include <iostream>

class Buzzer {
public:
    Buzzer(unsigned int pin = 4);
    bool begin();
    void on();
    void off();
    void beep(int duration_ms);

    ~Buzzer();

private:
    unsigned int gpio_pin;
    gpiod_chip *chip;
    gpiod_line *line;
};

#endif
