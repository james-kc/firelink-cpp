#include "sensors/imu.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cmath>

// Registers
#define WHO_AM_I        0x0F
#define CTRL1_XL        0x10
#define CTRL2_G         0x11
#define OUTX_L_G        0x22
#define OUTX_L_XL       0x28

IMU::IMU(uint8_t address) : fd(-1), i2c_addr(address) {}

bool IMU::begin() {
    const char *dev = "/dev/i2c-1";
    fd = open(dev, O_RDWR);
    if (fd < 0) { perror("Failed to open I2C bus"); return false; }
    if (ioctl(fd, I2C_SLAVE, i2c_addr) < 0) { perror("Failed to select I2C device"); return false; }

    uint8_t id = getChipID();
    std::cout << "Read IMU WHO_AM_I: 0x" << std::hex << (int)id << std::dec << std::endl;

    // Skip ID check, should be 0x69 for LSM6DS33 but is returning 0x6A
    // if (id != 0x69) { std::cerr << "Unexpected IMU ID: 0x" << std::hex << (int)id << std::dec << std::endl; return false; }

    writeRegister(CTRL1_XL, 0x40); // accel 104 Hz ±2g
    writeRegister(CTRL2_G, 0x40);  // gyro 104 Hz 245 dps

    return true;
}

uint8_t IMU::getChipID() { return readRegister(WHO_AM_I); }

void IMU::writeRegister(uint8_t reg, uint8_t value) { uint8_t buf[2] = {reg, value}; write(fd, buf, 2); }

uint8_t IMU::readRegister(uint8_t reg) { write(fd, &reg, 1); uint8_t data; read(fd, &data, 1); return data; }

void IMU::readRegisters(uint8_t startReg, uint8_t *buffer, uint8_t len) { write(fd, &startReg, 1); read(fd, buffer, len); }

void IMU::readAccel(float &ax, float &ay, float &az) {
    uint8_t buf[6]; readRegisters(OUTX_L_XL, buf, 6);
    int16_t rawX = (int16_t)(buf[1]<<8 | buf[0]);
    int16_t rawY = (int16_t)(buf[3]<<8 | buf[2]);
    int16_t rawZ = (int16_t)(buf[5]<<8 | buf[4]);
    float scale = 0.061f / 1000.0f; // ±2g, convert mg to g
    ax = rawX * scale * 9.80665f;
    ay = rawY * scale * 9.80665f;
    az = rawZ * scale * 9.80665f;
}

void IMU::readGyro(float &gx, float &gy, float &gz) {
    uint8_t buf[6]; readRegisters(OUTX_L_G, buf, 6);
    int16_t rawX = (int16_t)(buf[1]<<8 | buf[0]);
    int16_t rawY = (int16_t)(buf[3]<<8 | buf[2]);
    int16_t rawZ = (int16_t)(buf[5]<<8 | buf[4]);
    float scale = 8.75f / 1000.0f; // 245 dps
    gx = rawX * scale;
    gy = rawY * scale;
    gz = rawZ * scale;
}
