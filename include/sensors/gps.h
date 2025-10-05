#ifndef GPS_H
#define GPS_H

#include <cstdint>
#include <string>

class GPS {
public:
    GPS(uint8_t address = 0x10);
    bool begin();
    bool readSentence(std::string &sentence);
    bool getPosition(double &latitude, double &longitude, double &altitude, int &fixQuality, int &satellites);

private:
    int fd;
    uint8_t i2c_addr;

    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    void readRaw(char *buffer, uint8_t len);

    bool parseGGA(const std::string &sentence, double &latitude, double &longitude, double &altitude, int &fixQuality, int &satellites);
};

#endif
