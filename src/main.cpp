#include "sensors/bme280.h"
#include "sensors/imu.h"
#include "sensors/gps.h"
#include "outputs/buzzer.h"
#include "outputs/buzzer_pwm.h"
#include "notes.h"
#include <thread>
#include <chrono>
#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>
#include <algorithm>

void printGyroBars(float gx, float gy, float gz) {
    int scale = 10; // adjust sensitivity
    int lenX = std::min(40, std::max(0, (int)std::round(std::abs(gx) * scale)));
    int lenY = std::min(40, std::max(0, (int)std::round(std::abs(gy) * scale)));
    int lenZ = std::min(40, std::max(0, (int)std::round(std::abs(gz) * scale)));

    std::cout << "\r"; // return to start of line
    std::cout << "X [" << std::string(lenX, '=') << std::setw(40 - lenX) << "] "
              << "Y [" << std::string(lenY, '=') << std::setw(40 - lenY) << "] "
              << "Z [" << std::string(lenZ, '=') << std::setw(40 - lenZ) << "]   ";
    std::cout.flush();
}

void printTrace(float gx) {
    int pos = std::clamp(gx, -20, 20);

    for (int i = 0; i < 80; ++i)
        std::cout << (i == pos ? '*' : ' ');
    std::cout << '\n';
}

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
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }

    // IMU Test
    IMU imu;
    if (!imu.begin()) { std::cerr << "Failed to initialize IMU" << std::endl; return 1; }

    std::cout << "IMU initialized. ID: 0x" << std::hex << (int)imu.getChipID() << std::dec << std::endl;

    while (true) {
        float ax, ay, az, gx, gy, gz;
        imu.readAccel(ax, ay, az);
        imu.readGyro(gx, gy, gz);

        // std::cout << "Accel [m/s²]: X=" << ax << " Y=" << ay << " Z=" << az
        //           << " | Gyro [°/s]: X=" << gx << " Y=" << gy << " Z=" << gz << std::endl;

        // std::this_thread::sleep_for(std::chrono::seconds(1));

        // printGyroBars(gx, gy, gz);
        printTrace(az);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return 0;

    // GPS Test
    // GPS gps;
    // if (!gps.begin()) return 1;

    // while (true) {
    //     double lat, lon, alt;
    //     int fix, sats;
    //     if (gps.getPosition(lat, lon, alt, fix, sats)) {
    //         std::cout << "Lat: " << lat << ", Lon: " << lon
    //                   << ", Alt: " << alt << " m"
    //                   << ", Fix: " << fix
    //                   << ", Sats: " << sats << std::endl;
    //     } else {
    //         std::cout << "No valid GPS data..." << std::endl;
    //     }
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }

    // Buzzer Test
    // Buzzer buzzer;
    // if (!buzzer.begin()) return 1;

    // while (true) {
    //     buzzer.beep(200); // beep 200ms
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }

    // Buzzer PWM Test
    // BuzzerPWM buzzer;
    // if (!buzzer.begin()) return 1;

    // // Simple beep
    // buzzer.beep(200);

    // // Play 1kHz tone for 500ms
    // buzzer.tone(1000, 500);
    // std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // // Play a short major melody
    // int melody1[] = {
    //     Notes::C4, Notes::E4, Notes::G4
    // };
    // for (int note : melody1) {
    //     buzzer.tone(note, 175);
    // }

    // std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    // // Play a short minor melody
    // int melody2[] = {
    //     Notes::C4, Notes::Eb4, Notes::G4
    // };
    // for (int note : melody2) {
    //     buzzer.tone(note, 175);
    // }

    // std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    // // Majora's Mask "Item Catch" (Chest Open) melody
    // std::vector<int> chestOpen = {
    //     Notes::A5, Notes::Bb5, Notes::B5, Notes::C6
    // };
    // std::vector<int> durations = {
    //     100, 100, 100, 600 
    // };  // ms per note

    // buzzer.playMelody(chestOpen, durations);

}
