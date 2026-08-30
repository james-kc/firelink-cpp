#ifndef BME280_H
#define BME280_H

#include <cstdint>
#include <string>

// BME280 pressure / temperature / humidity sensor over I2C.
//
// begin() programs pressure oversampling x16 with an IIR low-pass filter,
// which behaves well under rocket vibration. Calibration values and the
// compensation maths follow the Bosch datasheet.
class BME280 {
public:
    BME280(const std::string &i2c_dev = "/dev/i2c-1", uint8_t address = 0x76);
    ~BME280();

    bool begin();
    float readTemperature(); // deg C
    float readPressure();    // hPa
    float readAltitude(float seaLevel_hPa = 1013.25f); // m
    float readHumidity();    // %RH

    // Average pressure over `samples` readings (~1 s apart) and return it.
    float calibrateAltitude(int samples = 10);

private:
    int fd_ = -1;
    uint8_t i2c_addr_;
    std::string i2c_dev_;
    int32_t t_fine = 0;

    struct CalibrationData {
        uint16_t dig_T1;
        int16_t dig_T2, dig_T3;
        uint16_t dig_P1;
        int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
        uint8_t dig_H1;
        int16_t dig_H2;
        uint8_t dig_H3;
        int16_t dig_H4, dig_H5;
        int8_t dig_H6;
    } calib{};

    uint8_t read8(uint8_t reg);
    uint16_t read16(uint8_t reg);
    int16_t readS16(uint8_t reg);
    uint32_t read24(uint8_t reg);
    void write8(uint8_t reg, uint8_t value);
    void readCalibration();
};

#endif // BME280_H
