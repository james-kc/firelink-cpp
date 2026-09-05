#ifndef GPS_H
#define GPS_H

#include <cstdint>
#include <string>
#include "sensors/nmea.h"

// PA1010D (GTop) GPS module, polled over I2C.
//
// The constructor only stores connection parameters; begin() opens the bus
// and configures NMEA output. readData() drains complete sentences from the
// module and returns a reference to the most recent fix state.
class GPS {
public:
    GPS(const std::string &i2c_dev = "/dev/i2c-1", uint8_t address = 0x10);
    ~GPS();

    // Opens the I2C bus and programs the module (GGA+RMC, update rate).
    bool begin(int update_hz = 1);

    // Reads available bytes, parses any completed sentences, and refreshes
    // the internal fix state. Returns true if at least one complete sentence
    // was parsed since the last call, false otherwise (so the caller can
    // detect a gone-silent module/bus instead of caching a stale fix).
    bool poll();

    const NmeaFix &fix() const { return fix_; }

    // Diagnostics: number of complete NMEA lines seen and how many of those
    // actually parsed (GGA/RMC), plus the most recent raw line.
    uint64_t lines() const { return lines_; }
    uint64_t sentences() const { return sentences_; }
    const std::string &lastLine() const { return last_line_; }

    void sendCommand(const std::string &cmd);

private:
    int         fd_ = -1;
    uint8_t     i2c_addr_;
    std::string i2c_dev_;
    std::string rx_buffer_;
    NmeaFix     fix_;
    uint64_t    lines_ = 0;
    uint64_t    sentences_ = 0;
    std::string last_line_;

    bool readLines();
};

#endif // GPS_H
