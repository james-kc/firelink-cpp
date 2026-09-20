#ifndef LORA_SX1272_H
#define LORA_SX1272_H

#include "telemetry/radio.h"

#include <cstddef>
#include <cstdint>
#include <gpiod.h>
#include <mutex>
#include <string>

// Driver for the RF Solutions RF-LORA-868-SO LoRa module (Semtech SX1272,
// 868 MHz) as the flight-side radio. The flight board connects it to the Pi
// Zero 2 W's SPI0 bus:
//
//   module SCLK       -> SPI0 SCLK   (BCM 11)
//   module SDIn(MOSI) -> SPI0 MOSI   (BCM 10)
//   module SDO        -> SPI0 MISO   (BCM 9)
//   module nSEL       -> SPI0 CE0    (BCM 8, spidev hardware chip-select)
//   module RESET      -> BCM 25
//   module RX_SWITCH  -> BCM 23
//   module TX_SWITCH  -> BCM 24
//
// RX_SWITCH/TX_SWITCH drive the module's on-board antenna switch: exactly one
// is high at a time (RX while listening, TX while transmitting). Never
// transmit with the switch still on RX - the PA has nowhere to go.
//
// Unlike the UART HAT this is a raw packet radio: frames are exact payload
// FIFO bytes with a fixed CRC, so both ends must agree on frequency, spreading
// factor, bandwidth, coding rate and sync word. Nothing here is transparent.
// The SX1272 register map differs from the SX1276 in several places; parent
// drivers (and the E22 HAT's on-air parameters, which are not readable) are
// not necessarily compatible - see docs for the interop procedure.
class LoRaSX1272 : public IRadio {
public:
    struct Settings {
        // SPI
        std::string spi_dev = "/dev/spidev0.0";
        uint32_t spi_speed_hz = 1000000;

        // GPIO (BCM numbering)
        std::string chip = "gpiochip0";
        int rst_pin = 25;
        int rx_switch_pin = 23;
        int tx_switch_pin = 24;

        // LoRa air parameters - every receiver must match these exactly.
        uint64_t freq_hz = 868125000;  // SX1272 HF: 860 .. 1020 MHz
        int sf = 9;                    // 7..12
        int bw_khz = 125;              // 125, 250, 500
        int cr = 5;                    // 4/5 .. 4/8
        uint8_t sync_word = 0x12;
        int preamble = 8;              // symbols
        int tx_power_dbm = 20;         // 2..20 on the SX1272 PA_BOOST

        bool rssi_append = false;      // append packet RSSI (dBm) to RX data
    };

    explicit LoRaSX1272(const Settings &settings);
    ~LoRaSX1272() override;

    bool begin() override;
    bool send(const uint8_t *data, size_t len) override;
    size_t receive(uint8_t *out, size_t max) override;
    int airtimeMs(size_t bytes) const override;
    bool rssiAppended() const override { return s_.rssi_append; }

    const Settings &settings() const { return s_; }

private:
    Settings s_;
    int spi_fd_ = -1;
    gpiod_chip *chip_ = nullptr;
    gpiod_line *rst_ = nullptr;
    gpiod_line *rx_sw_ = nullptr;
    gpiod_line *tx_sw_ = nullptr;
    std::mutex mu_;

    bool openSpi();
    bool requestPins();
    void setAntenna(bool tx);     // tx=true points the switch at TX

    // SPI/radio helpers; callers must hold mu_.
    void writeReg(uint8_t reg, uint8_t value);
    uint8_t readReg(uint8_t reg);
    void readFifo(uint8_t *out, size_t n);
    void setOpMode(uint8_t mode);       // mode bits [2:0], LoRa bit set
    bool waitForFlags(uint8_t want, int timeout_ms);
    void clearFlags(uint8_t flags);
};

#endif // LORA_SX1272_H