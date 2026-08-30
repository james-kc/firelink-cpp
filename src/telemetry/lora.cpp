#include "telemetry/lora.h"

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

namespace {

speed_t baudConstant(int baud) {
    switch (baud) {
        case 1200: return B1200;
        case 2400: return B2400;
        case 4800: return B4800;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 9600:
        default: return B9600;
    }
}

// EBYTE E22-style REG0/REG1/REG3 bit maps (see docs/telemetry_protocol.md;
// verify against the Waveshare wiki for your HAT revision).
uint8_t reg0For(int uart_baud, int air_baud) {
    int uart_bits;
    switch (uart_baud) {
        case 1200: uart_bits = 0; break;
        case 2400: uart_bits = 1; break;
        case 4800: uart_bits = 2; break;
        case 19200: uart_bits = 4; break;
        case 38400: uart_bits = 5; break;
        case 57600: uart_bits = 6; break;
        case 115200: uart_bits = 7; break;
        default: uart_bits = 3; break; // 9600
    }
    int air_bits;
    if (air_baud <= 300) air_bits = 0;
    else if (air_baud <= 1200) air_bits = 1;
    else if (air_baud <= 2400) air_bits = 2;
    else if (air_baud <= 4800) air_bits = 3;
    else if (air_baud <= 9600) air_bits = 4;
    else if (air_baud <= 19200) air_bits = 5;
    else if (air_baud <= 38400) air_bits = 6;
    else air_bits = 7;
    return (uint8_t)((uart_bits << 5) | air_bits); // parity 00 = 8N1
}

uint8_t reg1For(int tx_power_dbm) {
    int power_bits;
    if (tx_power_dbm >= 22) power_bits = 0;
    else if (tx_power_dbm >= 17) power_bits = 1;
    else if (tx_power_dbm >= 13) power_bits = 2;
    else power_bits = 3;
    return (uint8_t)power_bits; // sub-packet 240 B, ambient RSSI off
}

uint8_t reg3For(bool rssi_append, bool fixed_mode) {
    uint8_t v = 0;
    if (rssi_append) v |= 0x40; // RSSI byte appended to received data
    if (fixed_mode)  v |= 0x20; // fixed-point transmission
    return v;
}

} // namespace

LoRa::LoRa(const Settings &settings) : s_(settings) {}

LoRa::~LoRa() {
    if (uart_fd_ >= 0) close(uart_fd_);
    if (m0_) gpiod_line_release(m0_);
    if (m1_) gpiod_line_release(m1_);
    if (aux_) gpiod_line_release(aux_);
    if (chip_) gpiod_chip_close(chip_);
}

bool LoRa::requestPins() {
    chip_ = gpiod_chip_open_by_name(s_.chip.c_str());
    if (!chip_) {
        std::cerr << "LoRa: failed to open " << s_.chip << std::endl;
        return false;
    }
    m0_ = gpiod_chip_get_line(chip_, s_.m0_pin);
    m1_ = gpiod_chip_get_line(chip_, s_.m1_pin);
    if (!m0_ || !m1_) {
        std::cerr << "LoRa: failed to get M0/M1 lines (" << s_.m0_pin
                  << "/" << s_.m1_pin << ")" << std::endl;
        return false;
    }
    if (gpiod_line_request_output(m0_, "firelink-lora-m0", 0) < 0 ||
        gpiod_line_request_output(m1_, "firelink-lora-m1", 0) < 0) {
        std::cerr << "LoRa: failed to request M0/M1 outputs" << std::endl;
        return false;
    }
    if (s_.aux_pin >= 0) {
        aux_ = gpiod_chip_get_line(chip_, s_.aux_pin);
        if (aux_ && gpiod_line_request_input(aux_, "firelink-lora-aux") < 0) {
            std::cerr << "LoRa: failed to request AUX input, continuing without"
                      << std::endl;
            gpiod_line_release(aux_);
            aux_ = nullptr;
        }
    }
    return true;
}

void LoRa::setMode(int m0, int m1) {
    if (m0_) gpiod_line_set_value(m0_, m0);
    if (m1_) gpiod_line_set_value(m1_, m1);
    // Mode switches take a few milliseconds inside the module.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

bool LoRa::openUart() {
    uart_fd_ = open(s_.uart.c_str(), O_RDWR | O_NOCTTY);
    if (uart_fd_ < 0) {
        perror(("LoRa: failed to open " + s_.uart).c_str());
        return false;
    }

    termios tio;
    if (tcgetattr(uart_fd_, &tio) != 0) {
        perror("LoRa: tcgetattr failed");
        return false;
    }
    cfmakeraw(&tio);
    speed_t spd = baudConstant(s_.baud);
    cfsetispeed(&tio, spd);
    cfsetospeed(&tio, spd);
    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag &= ~CRTSCTS;
    tio.c_cflag &= ~CSTOPB;          // 1 stop bit
    tio.c_cflag &= ~PARENB;          // no parity
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;
    if (tcsetattr(uart_fd_, TCSANOW, &tio) != 0) {
        perror("LoRa: tcsetattr failed");
        return false;
    }
    tcflush(uart_fd_, TCIOFLUSH);
    return true;
}

bool LoRa::begin() {
    if (!requestPins()) return false;
    if (!openUart()) return false;

    if (s_.configure) {
        setMode(0, 1); // configuration mode
        bool ok = applyModuleConfig();
        setMode(0, 0); // back to transmission mode either way
        if (!ok) {
            std::cerr << "LoRa: module configuration failed, using existing "
                         "module settings" << std::endl;
        }
    } else {
        setMode(0, 0);
    }

    std::cout << "LoRa ready on " << s_.uart << " @ " << s_.baud
              << " baud (air " << s_.air_baud << ", ch " << (int)s_.channel
              << ")" << std::endl;
    return true;
}

bool LoRa::applyModuleConfig() {
    // Write registers: C2 ADDH(00) LEN(09) then 9 register bytes.
    uint8_t cfg[12] = {
        0xC2, 0x00, 0x09,
        (uint8_t)(s_.address >> 8), (uint8_t)(s_.address & 0xFF),
        s_.net_id,
        reg0For(s_.baud, s_.air_baud),
        reg1For(s_.tx_power_dbm),
        s_.channel,
        reg3For(s_.rssi_append, s_.fixed_mode),
        0x00, 0x00 // CRYPT H/L
    };

    tcflush(uart_fd_, TCIOFLUSH);
    if (write(uart_fd_, cfg, sizeof(cfg)) != (ssize_t)sizeof(cfg)) {
        perror("LoRa: config write failed");
        return false;
    }

    // Expect an echo beginning with C1 00 09 ...
    uint8_t resp[12] = {0};
    int got = 0;
    for (int tries = 0; tries < 20 && got < 3; ++tries) {
        ssize_t n = read(uart_fd_, resp + got, sizeof(resp) - got);
        if (n > 0) got += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (got >= 3 && resp[0] == 0xC1 && resp[1] == 0x00 && resp[2] == 0x09) {
        std::cout << "LoRa: module config acknowledged" << std::endl;
        return true;
    }
    return false;
}

int LoRa::airtimeMs(size_t bytes) const {
    int air = s_.air_baud > 0 ? s_.air_baud : 2400;
    // 10 bit-times per byte (8N1) + generous radio/FIFO overhead.
    return (int)((bytes * 10 * 1000) / air) + 60;
}

bool LoRa::waitAux(int timeout_ms) {
    if (!aux_) return false;
    for (int waited = 0; waited < timeout_ms; waited += 5) {
        if (gpiod_line_get_value(aux_) == 1) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool LoRa::send(const uint8_t *data, size_t len) {
    if (uart_fd_ < 0) return false;

    // Wait for the module to finish any previous transmission.
    if (aux_) waitAux(airtimeMs(len) * 2 + 250);

    size_t written = 0;
    while (written < len) {
        ssize_t n = write(uart_fd_, data + written, len - written);
        if (n < 0) {
            perror("LoRa: UART write failed");
            return false;
        }
        written += (size_t)n;
    }
    tcdrain(uart_fd_);

    if (!aux_) {
        // Without AUX we must pace transmissions ourselves.
        std::this_thread::sleep_for(std::chrono::milliseconds(airtimeMs(len)));
    }
    return true;
}
