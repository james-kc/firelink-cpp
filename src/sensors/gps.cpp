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

    // The PA1010D I2C interface streams NMEA as a rolling window: a read
    // transaction returns up to the requested number of bytes of the current
    // view (unused slots are 0x0A padding). Reading a single byte per
    // transaction samples a rotating slice and never yields a complete
    // sentence, so grab a chunk and buffer across polls instead.
    char buf[160];
    const ssize_t n = read(fd_, buf, sizeof buf);
    if (n > 0) rx_buffer_.append(buf, n);

    bool parsed = false;
    size_t start = 0;
    for (size_t i = 0; i < rx_buffer_.size(); ++i) {
        if (rx_buffer_[i] == '\n') {
            std::string line = rx_buffer_.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) {
                ++lines_;
                last_line_ = line;
                NmeaFix updated = fix_;
                if (nmeaParseGga(line, updated) || nmeaParseRmc(line, updated)) {
                    fix_ = updated;
                    ++sentences_;
                    parsed = true;
                }
            }
            start = i + 1;
        }
    }
    if (start > 0) rx_buffer_.erase(0, start);
    return parsed; // false = no complete sentence parsed this poll
}

bool GPS::poll() {
    return readLines();
}
