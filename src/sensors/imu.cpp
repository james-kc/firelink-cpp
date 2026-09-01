#include "sensors/imu.h"

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Registers
#define WHO_AM_I   0x0F
#define CTRL1_XL   0x10
#define CTRL2_G    0x11
#define OUTX_L_G   0x22
#define OUTX_L_XL  0x28

namespace {

// Full-scale selection bits (CTRL1_XL[3:2], CTRL2_G[3:2] + 125 dps bit).
uint8_t accelFsBits(int range_g, float &scale_g) {
    switch (range_g) {
        case 2:  scale_g = 0.000061f;  return 0x00;
        case 4:  scale_g = 0.000122f;  return 0x02 << 1;
        case 8:  scale_g = 0.000244f;  return 0x03 << 1;
        case 16:
        default: scale_g = 0.000488f;  return 0x01 << 1;
    }
}

// Gyro: FS bits differ (CTRL2_G[3:1]; 125 dps uses FS_125 bit 0).
uint8_t gyroFsBits(int range_dps, float &scale_dps) {
    switch (range_dps) {
        case 125: scale_dps = 0.004375f; return 0x01;        // FS_125
        case 250: scale_dps = 0.00875f;  return 0x00;
        case 500: scale_dps = 0.0175f;   return 0x02;
        case 1000: scale_dps = 0.035f;   return 0x04;
        case 2000:
        default:  scale_dps = 0.070f;    return 0x06;
    }
}

// ODR bits [7:4] shared by both control registers.
uint8_t odrBits(int hz) {
    if (hz <= 13)   return 0x10; // 12.5 Hz
    if (hz <= 39)   return 0x20; // 26 Hz
    if (hz <= 78)   return 0x30; // 52 Hz
    if (hz <= 156)  return 0x40; // 104 Hz
    if (hz <= 312)  return 0x50; // 208 Hz
    if (hz <= 624)  return 0x60; // 416 Hz
    if (hz <= 1249) return 0x70; // 833 Hz
    if (hz <= 2490) return 0x80; // 1.66 kHz
    if (hz <= 5000) return 0x90; // 3.33 kHz
    return 0xA0;                 // 6.66 kHz
}

} // namespace

IMU::IMU(const std::string &i2c_dev, uint8_t address)
    : i2c_addr_(address), i2c_dev_(i2c_dev) {}

IMU::~IMU() {
    if (fd_ >= 0) close(fd_);
}

bool IMU::begin(const Settings &settings) {
    fd_ = open(i2c_dev_.c_str(), O_RDWR);
    if (fd_ < 0) { perror("IMU: failed to open I2C bus"); return false; }
    if (ioctl(fd_, I2C_SLAVE, i2c_addr_) < 0) {
        perror("IMU: failed to select I2C device");
        close(fd_); fd_ = -1;
        return false;
    }

    uint8_t id = getChipID();
    std::cout << "IMU WHO_AM_I: 0x" << std::hex << (int)id << std::dec
              << std::endl;
    // LSM6DS3/33 normally report 0x69 but have been seen returning 0x6A;
    // warn but continue.
    if (id != 0x69 && id != 0x6A) {
        std::cerr << "IMU: unexpected WHO_AM_I, continuing anyway" << std::endl;
    }

    accel_scale_ = 0.000488f;
    gyro_scale_ = 0.070f;
    uint8_t xl_fs = accelFsBits(settings.accel_range_g, accel_scale_);
    uint8_t g_fs = gyroFsBits(settings.gyro_range_dps, gyro_scale_);
    uint8_t odr = odrBits(settings.odr_hz);

    writeRegister(CTRL1_XL, odr | xl_fs); // anti-alias BW tied to ODR
    writeRegister(CTRL2_G, odr | g_fs);

    return true;
}

uint8_t IMU::getChipID() { return readRegister(WHO_AM_I); }

void IMU::writeRegister(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    if (write(fd_, buf, 2) != 2) perror("IMU: register write failed");
}

uint8_t IMU::readRegister(uint8_t reg) {
    if (write(fd_, &reg, 1) != 1) return 0xFF;
    uint8_t data = 0;
    if (read(fd_, &data, 1) != 1) return 0xFF;
    return data;
}

void IMU::readRegisters(uint8_t startReg, uint8_t *buffer, uint8_t len) {
    if (write(fd_, &startReg, 1) != 1) return;
    if (read(fd_, buffer, len) != (ssize_t)len) {
        for (uint8_t i = 0; i < len; ++i) buffer[i] = 0;
    }
}

void IMU::readAccel(float &ax, float &ay, float &az) {
    uint8_t buf[6];
    readRegisters(OUTX_L_XL, buf, 6);
    int16_t rawX = (int16_t)(buf[1] << 8 | buf[0]);
    int16_t rawY = (int16_t)(buf[3] << 8 | buf[2]);
    int16_t rawZ = (int16_t)(buf[5] << 8 | buf[4]);
    ax = rawX * accel_scale_ * 9.80665f;
    ay = rawY * accel_scale_ * 9.80665f;
    az = rawZ * accel_scale_ * 9.80665f;
}

void IMU::readGyro(float &gx, float &gy, float &gz) {
    uint8_t buf[6];
    readRegisters(OUTX_L_G, buf, 6);
    int16_t rawX = (int16_t)(buf[1] << 8 | buf[0]);
    int16_t rawY = (int16_t)(buf[3] << 8 | buf[2]);
    int16_t rawZ = (int16_t)(buf[5] << 8 | buf[4]);
    gx = rawX * gyro_scale_;
    gy = rawY * gyro_scale_;
    gz = rawZ * gyro_scale_;
}
