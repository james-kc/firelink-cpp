#include "sensors/geiger.h"

#include <iostream>
#include <ctime>

Geiger::Geiger(const std::string &chip, unsigned int pin)
    : chip_name_(chip), pin_(pin) {}

Geiger::~Geiger() {
    if (line_) gpiod_line_release(line_);
    if (chip_) gpiod_chip_close(chip_);
}

bool Geiger::begin() {
    chip_ = gpiod_chip_open_by_name(chip_name_.c_str());
    if (!chip_) {
        std::cerr << "Geiger: failed to open " << chip_name_ << std::endl;
        return false;
    }

    line_ = gpiod_chip_get_line(chip_, pin_);
    if (!line_) {
        std::cerr << "Geiger: failed to get line " << pin_ << std::endl;
        return false;
    }

    int rc = gpiod_line_request_falling_edge_events(line_, "firelink-geiger");
    if (rc < 0) {
        std::cerr << "Geiger: failed to request edge events on pin "
                  << pin_ << std::endl;
        return false;
    }

    std::cout << "Geiger counter listening on " << chip_name_
              << " pin " << pin_ << std::endl;
    return true;
}

int Geiger::waitForPulses(int timeout_ms) {
    if (!line_) return 0;

    timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000L;

    int rc = gpiod_line_event_wait(line_, &ts);
    if (rc <= 0) return 0; // 0 = timeout, <0 = error (treated as timeout)

    gpiod_line_event ev;
    if (gpiod_line_event_read(line_, &ev) != 0) return 0;
    return 1;
}
