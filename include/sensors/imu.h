#ifndef IMU_H
#define IMU_H

#include <cstdint>
#include <string>

// LSM6DS3 / LSM6DS33 6-axis IMU over I2C.
//
// Ranges and output data rate are configurable because a rocket flight
// needs much wider ranges than the datasheet defaults (e.g. accel ±16 g,
// gyro 2000 dps, matching the flown firelink-py configuration).
class IMU {
public:
    struct Settings {
        int accel_range_g = 16;    // 2, 4, 8 or 16
        int gyro_range_dps = 2000; // 125, 250, 500, 1000 or 2000
        int odr_hz = 416;          // 12.5 is invalid here; nearest supported used
    };

    IMU(const std::string &i2c_dev = "/dev/i2c-1", uint8_t address = 0x6A);
    ~IMU();

    bool begin() { return begin(Settings()); }
    bool begin(const Settings &settings);
    void readAccel(float &ax, float &ay, float &az); // m/s^2
    void readGyro(float &gx, float &gy, float &gz);  // deg/s
    uint8_t getChipID();

private:
    int fd_ = -1;
    uint8_t i2c_addr_;
    std::string i2c_dev_;
    float accel_scale_ = 0.000488f; // g/LSB
    float gyro_scale_ = 0.070f;     // dps/LSB

    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    void readRegisters(uint8_t startReg, uint8_t *buffer, uint8_t len);
};

#endif // IMU_H
