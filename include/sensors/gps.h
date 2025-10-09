#ifndef GPS_H
#define GPS_H

#include <cstdint>
#include <string>
#include <optional>

struct GPSData {
    std::string datetime;
    bool hasFix;
    int fixQuality;
    double latitude;
    double longitude;
    double altitude_m;
    double speed_kmh;
    int satellites;
};

class GPS {
public:
    GPS(const std::string &i2c_dev = "/dev/i2c-1", uint8_t address = 0x10);
    ~GPS();

    bool begin();
    std::optional<GPSData> readData();
    void sendCommand(const std::string &cmd);

private:
    int fd;
    uint8_t i2c_addr;
    std::string buffer;
    GPSData lastData;

    bool hasFix(const std::string &nmea);
    std::optional<std::string> readLine();
    std::optional<GPSData> parseGGA(const std::string &nmea);
    std::optional<GPSData> parseRMC(const std::string &nmea);
};

#endif // GPS_H
