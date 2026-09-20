#ifndef RADIO_H
#define RADIO_H

#include <cstddef>
#include <cstdint>

// Abstract packet radio used by the flight computer. Two implementations:
//   LoRa       - Waveshare SX1262/SX1268 868 MHz UART HAT (transparent serial),
//   LoRaSX1272 - RF Solutions RF-LORA-868-SO (SX1272) packet radio on SPI.
//
// The interface deliberately carries no Linux headers so host-side tests
// (whose sources never link the radio drivers) keep building anywhere.
//
// Concurrency contract: send() and receive() must be safe to call
// concurrently from different threads. The telemetry thread sends frames
// while the link thread polls receive().
class IRadio {
public:
    virtual ~IRadio() = default;

    // Configure the radio and its IO. Returns false (without crashing) when
    // the hardware is missing/bad so the app can keep running with the link
    // down. Call once from the main thread before send()/receive().
    virtual bool begin() = 0;

    // Transmit one frame, blocking until the bytes are on the air.
    // Max payload is FIRELINK_PACKET_SIZE. Returns false on hard failure.
    virtual bool send(const uint8_t *data, size_t len) = 0;

    // Non-blocking drain of one received frame (plus, when rssiAppended(),
    // the trailing RSSI byte). Returns bytes written to `out`, 0 = nothing.
    // No-op while a concurrent send() holds the radio.
    virtual size_t receive(uint8_t *out, size_t max) = 0;

    // Estimated milliseconds a frame of `bytes` occupies the radio,
    // including preamble; used to pace transmissions.
    virtual int airtimeMs(size_t bytes) const = 0;

    // Whether receive() appends a two's-complement RSSI byte (dBm, negative
    // when valid) after a decoded frame. The link thread needs this to know
    // how to decode the byte stream.
    virtual bool rssiAppended() const = 0;
};

#endif // RADIO_H