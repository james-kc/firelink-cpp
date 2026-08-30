#ifndef GEIGER_H
#define GEIGER_H

#include <string>
#include <cstdint>
#include <gpiod.h>

// Gravity: Geiger Counter Module (DFRobot SEN0463).
//
// The module emits a short pulse on its signal pin for every detected
// ionising particle. We count falling edges with libgpiod edge events;
// the caller thread converts pulse timestamps into windowed counts and CPM.
class Geiger {
public:
    Geiger(const std::string &chip = "gpiochip0", unsigned int pin = 17);
    ~Geiger();

    bool begin();

    // Wait up to `timeout_ms` for the next pulse.
    // Returns the number of pulse events observed (0 on timeout).
    // Intended to be called in a tight loop from a dedicated thread.
    int waitForPulses(int timeout_ms);

private:
    std::string chip_name_;
    unsigned int pin_;
    gpiod_chip *chip_ = nullptr;
    gpiod_line *line_ = nullptr;
};

#endif // GEIGER_H
