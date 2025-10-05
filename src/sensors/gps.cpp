#include "sensors/gps.h"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <sstream>
#include <vector>

// Default I2C address for PA1010D is 0x10

GPS::GPS(uint8_t address) : fd(-1), i2c_addr(address) {}

bool GPS::begin() {
    const char *dev = "/dev/i2c-1";
    fd = open(dev, O_RDWR);
    if (fd < 0) { perror("Failed to open I2C bus"); return false; }
    if (ioctl(fd, I2C_SLAVE, i2c_addr) < 0) { perror("Failed to select I2C device"); return false; }

    std::cout << "GPS initialized on " << dev << " (I2C address 0x"
              << std::hex << (int)i2c_addr << std::dec << ")" << std::endl;
    return true;
}

void GPS::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    write(fd, buf, 2);
}

uint8_t GPS::readRegister(uint8_t reg) {
    write(fd, &reg, 1);
    uint8_t data;
    read(fd, &data, 1);
    return data;
}

void GPS::readRaw(char *buffer, uint8_t len) {
    ssize_t bytes = read(fd, buffer, len);
    if (bytes > 0) buffer[bytes] = '\0';
    else buffer[0] = '\0';
}

bool GPS::readSentence(std::string &sentence) {
    char buffer[128];
    readRaw(buffer, sizeof(buffer) - 1);
    sentence = buffer;

    // Check if it contains a valid NMEA sentence
    if (sentence.find("$GP") == std::string::npos) return false;
    return true;
}

bool GPS::getPosition(double &latitude, double &longitude, double &altitude, int &fixQuality, int &satellites) {
    std::string sentence;
    if (!readSentence(sentence)) return false;
    if (sentence.find("GGA") == std::string::npos) return false;

    return parseGGA(sentence, latitude, longitude, altitude, fixQuality, satellites);
}

bool GPS::parseGGA(const std::string &sentence, double &latitude, double &longitude, double &altitude, int &fixQuality, int &satellites) {
    // Example: $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47
    std::vector<std::string> fields;
    std::stringstream ss(sentence);
    std::string token;
    while (std::getline(ss, token, ',')) fields.push_back(token);

    if (fields.size() < 10) return false;

    try {
        double rawLat = std::stod(fields[2]);
        double rawLon = std::stod(fields[4]);
        fixQuality = std::stoi(fields[6]);
        satellites = std::stoi(fields[7]);
        altitude = std::stod(fields[9]);

        // Convert from DDMM.MMMM to decimal degrees
        double latDeg = int(rawLat / 100);
        double lonDeg = int(rawLon / 100);
        latitude = latDeg + (rawLat - latDeg * 100) / 60.0;
        longitude = lonDeg + (rawLon - lonDeg * 100) / 60.0;
        if (fields[3] == "S") latitude = -latitude;
        if (fields[5] == "W") longitude = -longitude;

        return true;
    } catch (...) {
        return false;
    }
}
