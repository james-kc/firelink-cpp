#include "sensors/bme280.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <cmath>
#include <algorithm>

#define REG_ID        0xD0
#define REG_CTRL_HUM  0xF2
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG    0xF5
#define REG_PRESS_MSB 0xF7
#define REG_TEMP_MSB  0xFA
#define REG_HUM_MSB   0xFD

BME280::BME280(const std::string &i2c_dev, uint8_t address)
    : i2c_addr_(address), i2c_dev_(i2c_dev) {}

BME280::~BME280() {
    if (fd_ >= 0) close(fd_);
}

bool BME280::begin() {
    fd_ = open(i2c_dev_.c_str(), O_RDWR);
    if (fd_ < 0) {
        perror(("BME280: failed to open " + i2c_dev_).c_str());
        return false;
    }
    if (ioctl(fd_, I2C_SLAVE, i2c_addr_) < 0) {
        perror("BME280: failed to set I2C address");
        close(fd_);
        fd_ = -1;
        return false;
    }

    uint8_t id = read8(REG_ID);
    if (id != 0x60) {
        std::cerr << "BME280: unexpected chip ID 0x" << std::hex << (int)id
                  << std::dec << " (expected 0x60)" << std::endl;
        return false;
    }

    readCalibration();

    // Humidity oversampling x1.
    write8(REG_CTRL_HUM, 0x01);
    // Normal mode, temperature oversampling x1, pressure oversampling x16.
    write8(REG_CTRL_MEAS, 0x33); // osrs_t=001, osrs_p=100, mode=11
    // Standby 62.5 ms, IIR filter coefficient 16.
    write8(REG_CONFIG, 0x50);    // t_sb=010, filter=100

    return true;
}

void BME280::write8(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    if (write(fd_, buf, 2) != 2) perror("BME280: register write failed");
}

uint8_t BME280::read8(uint8_t reg) {
    if (write(fd_, &reg, 1) != 1) return 0;
    uint8_t val = 0;
    if (read(fd_, &val, 1) != 1) return 0;
    return val;
}

uint16_t BME280::read16(uint8_t reg) {
    uint8_t lsb = read8(reg);
    uint8_t msb = read8(reg + 1);
    return (uint16_t)((msb << 8) | lsb);
}

int16_t BME280::readS16(uint8_t reg) {
    return (int16_t)read16(reg);
}

uint32_t BME280::read24(uint8_t reg) {
    uint8_t buf[3];
    if (write(fd_, &reg, 1) != 1) return 0;
    if (read(fd_, buf, 3) != 3) return 0;
    return (static_cast<uint32_t>(buf[0]) << 16) | (buf[1] << 8) | buf[2];
}

void BME280::readCalibration() {
    calib.dig_T1 = read16(0x88);
    calib.dig_T2 = readS16(0x8A);
    calib.dig_T3 = readS16(0x8C);

    calib.dig_P1 = read16(0x8E);
    calib.dig_P2 = readS16(0x90);
    calib.dig_P3 = readS16(0x92);
    calib.dig_P4 = readS16(0x94);
    calib.dig_P5 = readS16(0x96);
    calib.dig_P6 = readS16(0x98);
    calib.dig_P7 = readS16(0x9A);
    calib.dig_P8 = readS16(0x9C);
    calib.dig_P9 = readS16(0x9E);

    calib.dig_H1 = read8(0xA1);
    calib.dig_H2 = readS16(0xE1);
    calib.dig_H3 = read8(0xE3);

    uint8_t e4 = read8(0xE4);
    uint8_t e5 = read8(0xE5);
    uint8_t e6 = read8(0xE6);

    calib.dig_H4 = (e4 << 4) | (e5 & 0x0F);
    calib.dig_H5 = (e6 << 4) | (e5 >> 4);
    calib.dig_H6 = static_cast<int8_t>(read8(0xE7));
}

float BME280::readTemperature() {
    int32_t adc_T = read24(REG_TEMP_MSB) >> 4;

    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calib.dig_T1 << 1))) * ((int32_t)calib.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)calib.dig_T1)) * ((adc_T >> 4) - ((int32_t)calib.dig_T1))) >> 12) * ((int32_t)calib.dig_T3)) >> 14;

    t_fine = var1 + var2;
    float T = (t_fine * 5 + 128) >> 8;
    return T / 100.0f;
}

float BME280::readPressure() {
    readTemperature(); // updates t_fine
    int32_t adc_P = read24(REG_PRESS_MSB) >> 4;

    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)calib.dig_P3) >> 8) + ((var1 * (int64_t)calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)calib.dig_P1) >> 33;

    if (var1 == 0) return 0;

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib.dig_P7) << 4);

    return (float)p / 25600.0f;
}

float BME280::readAltitude(float seaLevel_hPa) {
    float pressure = readPressure();
    return 44330.0f * (1.0f - std::pow(pressure / seaLevel_hPa, 0.1903f));
}

float BME280::readHumidity() {
    readTemperature(); // updates t_fine

    int32_t adc_H = (read8(REG_HUM_MSB) << 8) | read8(REG_HUM_MSB + 1);

    int32_t v_x1_u32r = t_fine - 76800;
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)calib.dig_H4) << 20) - (((int32_t)calib.dig_H5) * v_x1_u32r)) + 16384) >> 15) *
                 (((((((v_x1_u32r * (int32_t)calib.dig_H6) >> 10) * (((v_x1_u32r * (int32_t)calib.dig_H3) >> 11) + 32768)) >> 10) + 2097152) *
                    (int32_t)calib.dig_H2 + 8192) >> 14));
    v_x1_u32r = v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * (int32_t)calib.dig_H1) >> 4);
    v_x1_u32r = std::max(0, std::min(v_x1_u32r, 419430400));
    return (v_x1_u32r >> 12) / 1024.0f;
}

float BME280::calibrateAltitude(int samples) {
    std::cout << "Measuring pad pressure for altitude calibration..." << std::endl;

    float sum = 0.0f;
    for (int i = 0; i < samples; ++i) {
        sum += readPressure();
        sleep(1);
    }

    float pad_pressure = sum / samples;
    std::cout << "Altitude calibration complete. Pad pressure: "
              << pad_pressure << " hPa" << std::endl;
    return pad_pressure;
}
