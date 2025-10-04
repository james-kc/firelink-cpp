#include "bme280.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <cmath>

#define REG_ID 0xD0
#define REG_CTRL_HUM 0xF2
#define REG_STATUS 0xF3
#define REG_CTRL_MEAS 0xF4
#define REG_CONFIG 0xF5
#define REG_PRESS_MSB 0xF7
#define REG_HUM_MSB 0xFD

#define REG_PRESS_LSB  0xF8
#define REG_PRESS_XLSB 0xF9
#define REG_TEMP_MSB   0xFA
#define REG_TEMP_LSB   0xFB
#define REG_TEMP_XLSB  0xFC
#define REG_HUM_LSB    0xFE


BME280::BME280(const std::string &i2c_dev, uint8_t address)
    : i2c_addr(address), fd(-1), t_fine(0) 
{
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

BME280::~BME280() {
    if (fd >= 0) close(fd);
}

bool BME280::begin() {
    // 🔍 Read and print the chip ID
    uint8_t id = read8(REG_ID);
    std::cout << "BME280 ID: 0x" << std::hex << (int)id << std::dec << std::endl;

    if (id != 0x60) {
        std::cerr << "Unexpected chip ID! (expected 0x60)" << std::endl;
        return false;
    }

    // Read calibration data
    readCalibration();
    // printCalibration();


    // Set humidity oversampling x1
    uint8_t buf_hum[2] = {REG_CTRL_HUM, 0x01};
    write(fd, buf_hum, 2);

    // Set normal mode, temp and pressure oversampling x1
    uint8_t buf_meas[2] = {REG_CTRL_MEAS, 0x27};
    write(fd, buf_meas, 2);

    // Set config (standby 1000ms)
    uint8_t buf_config[2] = {REG_CONFIG, 0xA0};
    write(fd, buf_config, 2);

    return true;
}

uint8_t BME280::read8(uint8_t reg) {
    uint8_t val;
    write(fd, &reg, 1);
    read(fd, &val, 1);
    return val;
}

uint16_t BME280::read16(uint8_t reg) {
    uint8_t lsb = read8(reg);       // read LSB first
    uint8_t msb = read8(reg + 1);   // then MSB
    return (msb << 8) | lsb;        // combine
}

int16_t BME280::readS16(uint8_t reg) {
    return (int16_t)read16(reg);
}

uint32_t BME280::read24(uint8_t reg) {
    uint8_t buf[3];
    write(fd, &reg, 1);
    read(fd, buf, 3);
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


void BME280::printCalibration() {
    printf("Calibration values:\n");
    printf("T1: %u\n", calib.dig_T1);
    printf("T2: %d\n", calib.dig_T2);
    printf("T3: %d\n", calib.dig_T3);

    printf("P1: %u\n", calib.dig_P1);
    printf("P2: %d\n", calib.dig_P2);
    printf("P3: %d\n", calib.dig_P3);
    printf("P4: %d\n", calib.dig_P4);
    printf("P5: %d\n", calib.dig_P5);
    printf("P6: %d\n", calib.dig_P6);
    printf("P7: %d\n", calib.dig_P7);
    printf("P8: %d\n", calib.dig_P8);
    printf("P9: %d\n", calib.dig_P9);

    printf("H1: %u\n", calib.dig_H1);
    printf("H2: %d\n", calib.dig_H2);
    printf("H3: %u\n", calib.dig_H3);
    printf("H4: %d\n", calib.dig_H4);
    printf("H5: %d\n", calib.dig_H5);
    printf("H6: %d\n", calib.dig_H6);
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

    if (var1 == 0) return 0; // avoid div by zero

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calib.dig_P7) << 4);

    return (float)p / 25600.0f; // convert to hPa
}

float BME280::readAltitude(float seaLevel_hPa) {
    float pressure = readPressure();  // in hPa
    return 44330.0f * (1.0f - pow(pressure / seaLevel_hPa, 0.1903f));
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

float BME280::calibrateAltitude() {
    std::cout << "Measuring pad pressure for altitude calibration..." << std::endl;
    
    float pressure_sum = 0.0f;
    
    for (int i = 0; i < 10; ++i) {
        pressure_sum += readPressure();
        sleep(1);        
    }

    float pad_pressure = pressure_sum / 10.0f;

    std::cout << " done. Pad pressure: " << pad_pressure << " hPa" << std::endl;

    return pad_pressure;
}