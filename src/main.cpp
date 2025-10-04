#include "sensors/bme280.h"
#include <iostream>
#include <unistd.h>

int main() {
    BME280 sensor;
    sensor.begin();

    while (true) {
        float temp = sensor.readTemperature();
        float pressure = sensor.readPressure();
        float humidity = sensor.readHumidity();
        std::cout << "Temp: " << temp << " °C, "
                  << "Pressure: " << pressure << " hPa, "
                  << "Humidity: " << humidity << " %" << std::endl;
        sleep(1);
    }
    return 0;
}
