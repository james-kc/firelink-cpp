#ifndef BUZZER_H
#define BUZZER_H

#include <cstdint>

class Buzzer {
public:
    Buzzer(uint8_t gpio_pin = 4);
    bool begin();
    void on();
    void off();
    void beep(int duration_ms); // turns on for duration_ms milliseconds

private:
    uint8_t pin;
    void writeGPIO(const char *value);
};

#endif
