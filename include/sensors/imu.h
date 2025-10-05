#ifndef IMU_H
#define IMU_H

#include <cstdint>

class IMU {
public:
    IMU(uint8_t address = 0x6A);
    bool begin();
    void readAccel(float &ax, float &ay, float &az);
    void readGyro(float &gx, float &gy, float &gz);
    uint8_t getChipID();

private:
    int fd;
    uint8_t i2c_addr;

    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    void readRegisters(uint8_t startReg, uint8_t *buffer, uint8_t len);
};

#endif
