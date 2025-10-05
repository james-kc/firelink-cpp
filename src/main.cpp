#include "sensors/bme280.h"
#include "sensors/imu.h"
#include "sensors/gps.h"
#include "outputs/buzzer.h"
#include "outputs/buzzer_pwm.h"
#include <thread>
#include <chrono>
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
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }

    // IMU Test
    // IMU imu;
    // if (!imu.begin()) { std::cerr << "Failed to initialize IMU" << std::endl; return 1; }

    // std::cout << "IMU initialized. ID: 0x" << std::hex << (int)imu.getChipID() << std::dec << std::endl;

    // while (true) {
    //     float ax, ay, az, gx, gy, gz;
    //     imu.readAccel(ax, ay, az);
    //     imu.readGyro(gx, gy, gz);

    //     std::cout << "Accel [m/s²]: X=" << ax << " Y=" << ay << " Z=" << az
    //               << " | Gyro [°/s]: X=" << gx << " Y=" << gy << " Z=" << gz << std::endl;

    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }

    // return 0;

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
    BuzzerPWM buzzer;
    if (!buzzer.begin()) return 1;

    // Simple beep
    buzzer.beep(200);

    // Play 1kHz tone for 500ms
    buzzer.tone(1000, 500);
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Play a short melody
    int melody1[] = {262, 330, 392}; // C4-E4-G4
    for (int note : melody1) {
        buzzer.tone(note, 175);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Play a short melody, double speed
    int melody2[] = {262, 330, 392, 262, 330, 392}; // C4-E4-G4
    for (int note : melody2) {
        buzzer.tone(note, 87.5);
    }

    // C major “chest open” melody
    int chestNotes[] = {523, 659, 783, 1046}; // C5, E5, G5, C6
    int durations[]  = {100, 100, 100, 150};  // milliseconds per note
    int numNotes = sizeof(chestNotes) / sizeof(chestNotes[0]);

    buzzer.playMelody(chestNotes, durations, 4);


}
