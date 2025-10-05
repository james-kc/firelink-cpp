#include "sensors/bme280.h"
#include "sensors/imu.h"
#include <iostream>
#include <unistd.h>

int main() {

    // BME280 Test
    // BME280 bme;
    // bme.begin();
    // float pad_pressure = bme.calibrateAltitude();

    // while (true) {
    //     float temp = bme.readTemperature();
    //     float pressure = bme.readPressure();
    //     float altitude = bme.readAltitude(pad_pressure);
    //     float humidity = bme.readHumidity();
    //     std::cout << "Temp: " << temp << " °C, "
    //               << "Pressure: " << pressure << " hPa, "
    //               << "Altitude: " << altitude << " m, "
    //               << "Humidity: " << humidity << " %" << std::endl;
    //     sleep(1);
    // }

    // IMU Test
    IMU imu;
    if (!imu.begin()) { std::cerr << "Failed to initialize IMU" << std::endl; return 1; }

    std::cout << "IMU initialized. ID: 0x" << std::hex << (int)imu.getChipID() << std::dec << std::endl;

    while (true) {
        float ax, ay, az, gx, gy, gz;
        imu.readAccel(ax, ay, az);
        imu.readGyro(gx, gy, gz);

        std::cout << "Accel [m/s²]: X=" << ax << " Y=" << ay << " Z=" << az
                  << " | Gyro [°/s]: X=" << gx << " Y=" << gy << " Z=" << gz << std::endl;

        sleep(1);
    }

    return 0;
}
