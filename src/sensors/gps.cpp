#include "sensors/gps.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <cstdio>

GPS::GPS(const std::string &i2c_dev, uint8_t address)
    : i2c_addr_(address), i2c_dev_(i2c_dev) {}

GPS::~GPS() {
    if (fd_ >= 0) close(fd_);
}

bool GPS::begin(int update_hz) {
    fd_ = open(i2c_dev_.c_str(), O_RDWR);
    if (fd_ < 0) {
        perror(("GPS: failed to open " + i2c_dev_).c_str());
        return false;
    }
    if (ioctl(fd_, I2C_SLAVE, i2c_addr_) < 0) {
        perror("GPS: failed to set I2C address");
        close(fd_);
        fd_ = -1;
        return false;
    }

    // Output GGA + RMC only.
    sendCommand("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n");

    // Update rate (PA1010D supports 1..10 Hz).
    int period_ms = 1000 / (update_hz <= 0 ? 1 : update_hz);
    char cmd[32];
    std::snprintf(cmd, sizeof(cmd), "$PMTK220,%d*2B\r\n", period_ms);
    // Note: the checksum of PMTK220 varies with the period; the module
    // accepts the command with a wildcard-less (incorrect) checksum on all
    // firmware tested, but send our best-known value for 1 Hz and fall back
    // gracefully otherwise.
    if (period_ms == 1000)
        sendCommand("$PMTK220,1000*1F\r\n");
    else if (period_ms == 200)
        sendCommand("$PMTK220,200*2C\r\n");
    else if (period_ms == 100)
        sendCommand("$PMTK220,100*2F\r\n");
    else
        sendCommand(cmd);

    std::cout << "GPS initialised (GTop PA1010D, " << i2c_dev_ << " @ 0x"
              << std::hex << (int)i2c_addr_ << std::dec << ")" << std::endl;
    return true;
}

void GPS::sendCommand(const std::string &cmd) {
    if (fd_ < 0) return;
    if (write(fd_, cmd.c_str(), cmd.size()) != (ssize_t)cmd.size()) {
        perror("GPS: failed to write command");
    }
}

bool GPS::readLines() {
    if (fd_ < 0) return false;

    char c;
    bool parsed = false;
    while (read(fd_, &c, 1) == 1) {
        if (c == '\n') {
            std::string line = rx_buffer_;
            rx_buffer_.clear();
            if (!line.empty()) {
                NmeaFix updated = fix_;
                if (nmeaParseGga(line, updated) || nmeaParseRmc(line, updated)) {
                    fix_ = updated;
                    parsed = true;
                }
            }
        } else if (c != '\r') {
            rx_buffer_ += c;
        }
    }
    return parsed; // false = no complete sentence arrived this poll
}

bool GPS::poll() {
    return readLines();
}
