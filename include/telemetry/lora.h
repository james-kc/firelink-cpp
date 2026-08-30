#ifndef LORA_H
#define LORA_H

#include <string>
#include <cstdint>
#include <cstddef>
#include <gpiod.h>

// Driver for the Waveshare SX1262/SX1268 LoRa HAT. These HATs carry an
// EBYTE-E22-style module that presents a UART to the host: bytes written to
// the serial port are transmitted over the air ("transparent transmission")
// and arrive at the peer's UART.
//
// M0/M1 GPIOs select the module operating mode:
//   M0=0 M1=0 : transmission (normal)
//   M0=1 M1=0 : WOR
//   M0=0 M1=1 : configuration
//   M0=1 M1=1 : deep sleep
// (EBYTE convention - verify against the wiki page for your HAT revision.)
//
// An optional AUX pin reports when the radio is busy; if wired it is polled
// before large writes, otherwise transmission timing is estimated from the
// configured air data rate.
class LoRa {
public:
    struct Settings {
        std::string uart = "/dev/serial0";
        int baud = 9600;              // UART baud (module must match)
        int m0_pin = 22;              // BCM numbering
        int m1_pin = 27;
        int aux_pin = -1;             // -1 = not wired
        std::string chip = "gpiochip0";

        bool configure = false;       // push the register config below at boot
        uint16_t address = 0;
        uint8_t net_id = 0;
        int air_baud = 2400;          // 300,1200,2400,4800,9600,19200,38400,62500
        int tx_power_dbm = 22;        // 22, 17, 13 or 10
        uint8_t channel = 18;         // 868.125 MHz on the 868 MHz HAT
        bool rssi_append = false;     // module appends an RSSI byte to RX packets
        bool fixed_mode = false;      // fixed-point (addressed) transmission
    };

    explicit LoRa(const Settings &settings);
    ~LoRa();

    bool begin();

    // Push configuration registers (C2 command block). Only call in
    // configuration mode; begin() handles that when settings.configure=true.
    bool applyModuleConfig();

    // Queue bytes for transmission. Blocks until written (waiting for AUX
    // when available). Returns false on hard failure.
    bool send(const uint8_t *data, size_t len);

    // Milliseconds a frame of `bytes` is expected to occupy the radio,
    // based on the configured air data rate. Used for pacing when AUX is
    // not wired.
    int airtimeMs(size_t bytes) const;

    const Settings &settings() const { return s_; }

private:
    Settings s_;
    int uart_fd_ = -1;
    gpiod_chip *chip_ = nullptr;
    gpiod_line *m0_ = nullptr;
    gpiod_line *m1_ = nullptr;
    gpiod_line *aux_ = nullptr;

    bool openUart();
    bool requestPins();
    void setMode(int m0, int m1);
    bool waitAux(int timeout_ms);
};

#endif // LORA_H
