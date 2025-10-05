#ifndef BUZZER_PWM_H
#define BUZZER_PWM_H

#include <cstdint>

class BuzzerPWM {
public:
    BuzzerPWM(uint8_t gpio_pin = 4);
    bool begin();

    void on();
    void off();
    void beep(int duration_ms);                 // Simple on/off beep
    void tone(int frequency, int duration_ms); // PWM tone
    void noTone();                              // Stop any ongoing tone

private:
    uint8_t pin;
    bool tone_active;

    void writeGPIO(const char *value);
};

#endif
