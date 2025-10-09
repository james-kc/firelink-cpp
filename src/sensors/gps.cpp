#include "sensors/gps.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <algorithm> // for std::clamp, std::all_of
#include <cctype>    // for std::isdigit

// Helper to safely check if a string is numeric
static bool isNumeric(const std::string &s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isdigit(c) || c == '.' || c == '-' || c == '+';
    });
}

// Helper to check string prefix (for C++17 compatibility)
static bool startsWith(const std::string &s, const std::string &prefix) {
    return s.rfind(prefix, 0) == 0;
}

GPS::GPS(const std::string &i2c_dev, uint8_t address)
    : fd(-1), i2c_addr(address) {
    fd = open(i2c_dev.c_str(), O_RDWR);
    if (fd < 0) {
        perror("Failed to open I2C device");
        exit(1);
    }
    if (ioctl(fd, I2C_SLAVE, i2c_addr) < 0) {
        perror("Failed to set I2C address");
        exit(1);
    }
}

GPS::~GPS() {
    if (fd >= 0) close(fd);
}

bool GPS::begin() {
    // Configure NMEA output (GGA + RMC)
    sendCommand("$PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*28\r\n");
    // 1 Hz update rate
    sendCommand("$PMTK220,1000*1F\r\n");
    std::cout << "GPS initialized (GTop PA1010D)" << std::endl;
    return true;
}

void GPS::sendCommand(const std::string &cmd) {
    if (write(fd, cmd.c_str(), cmd.size()) != (ssize_t)cmd.size()) {
        perror("Failed to write GPS command");
    }
}

std::optional<std::string> GPS::readLine() {
    char c;
    while (read(fd, &c, 1) == 1) {
        if (c == '\n') {
            std::string line = buffer;
            buffer.clear();
            return line;
        } else if (c != '\r') {
            buffer += c;
        }
    }
    return std::nullopt;
}

std::optional<GPSData> GPS::readData() {
    auto lineOpt = readLine();
    if (!lineOpt) return std::nullopt;

    const std::string &line = *lineOpt;

    // Optional: uncomment to see incoming data
    // std::cout << "[GPS] " << line << std::endl;

    if (startsWith(line, "$GPGGA") || startsWith(line, "$GNGGA")) {
        return parseNMEA(line);
    }
    return std::nullopt;
}

bool GPS::hasFix(const std::string &nmea) {
    std::vector<std::string> tokens;
    std::stringstream ss(nmea);
    std::string token;
    while (std::getline(ss, token, ',')) tokens.push_back(token);

    if (tokens.size() > 6) {
        return tokens[6] != "0"; // fix quality > 0 means we have a fix
    }
    return false;
}

std::optional<GPSData> GPS::parseNMEA(const std::string &nmea) {
    std::vector<std::string> tokens;
    std::stringstream ss(nmea);
    std::string token;
    while (std::getline(ss, token, ',')) tokens.push_back(token);

    if (tokens.size() < 15) return std::nullopt;

    GPSData data{};
    data.hasFix = tokens[6] != "0";

    // Fix quality
    if (isNumeric(tokens[6])) data.fixQuality = std::stoi(tokens[6]);
    else data.fixQuality = 0;

    // Satellites
    if (isNumeric(tokens[7])) data.satellites = std::stoi(tokens[7]);
    else data.satellites = 0;

    // Altitude
    if (isNumeric(tokens[9])) data.altitude_m = std::stod(tokens[9]);
    else data.altitude_m = 0.0;

    // Latitude
    if (isNumeric(tokens[2]) && !tokens[3].empty()) {
        double lat = std::stod(tokens[2].substr(0, 2))
                   + std::stod(tokens[2].substr(2)) / 60.0;
        if (tokens[3] == "S") lat = -lat;
        data.latitude = lat;
    } else {
        data.latitude = 0.0;
    }

    // Longitude
    if (isNumeric(tokens[4]) && !tokens[5].empty()) {
        double lon = std::stod(tokens[4].substr(0, 3))
                   + std::stod(tokens[4].substr(3)) / 60.0;
        if (tokens[5] == "W") lon = -lon;
        data.longitude = lon;
    } else {
        data.longitude = 0.0;
    }

    data.datetime = tokens[1]; // raw UTC time string

    return data;
}
