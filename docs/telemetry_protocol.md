# Firelink telemetry protocol v1

The flight computer transmits a fixed **48-byte** frame over the Waveshare
SX1262 LoRa HAT (UART transparent transmission: bytes in one side appear at
the other). The ground station (`groundstation/`) parses the same format —
this document is the contract between them.

Reference implementations:

- C++ encoder/decoder: `include/telemetry/packet.h`, `src/telemetry/packet.cpp`
- Python decoder/encoder: `groundstation/firelink_gs/packet.py`

## Frame layout (all integers little-endian)

| Offset | Size | Field              | Type   | Notes                                  |
|--------|------|--------------------|--------|----------------------------------------|
| 0      | 2    | magic              | bytes  | ASCII `F` `L` (0x46 0x4C)               |
| 2      | 1    | version            | u8     | protocol version, currently 1          |
| 3      | 1    | type               | u8     | 1 = telemetry, 2 = status text         |
| 4      | 2    | seq                | u16    | wrapping sequence number (loss detect) |
| 6      | 1    | state              | u8     | 0 BOOT, 1 PREFLIGHT, 2 ARMED, 3 LANDED |
| 7      | 4    | t_ms               | u32    | ms since arm (0 before arming)         |
| 11     | 35   | payload            | —      | see below                              |
| 46     | 2    | crc16              | u16    | CRC-16/CCITT-FALSE over bytes 0..45    |

Python struct format for a full telemetry frame (both sides must agree):

```
<2sBBHBIiihBBHhB6hHHH
```

## Type 1 — telemetry payload (bytes 11..45)

| Offset | Size | Field                | Type | Units / scale          |
|--------|------|----------------------|------|------------------------|
| 11     | 4    | lat                  | i32  | degrees × 1e7          |
| 15     | 4    | lon                  | i32  | degrees × 1e7          |
| 19     | 2    | gps_alt_m            | i16  | m                      |
| 21     | 1    | satellites           | u8   | count                  |
| 22     | 1    | fix_quality          | u8   | NMEA fix quality       |
| 23     | 2    | speed                | u16  | 0.1 km/h               |
| 25     | 2    | baro_alt_rel_m       | i16  | m above pad            |
| 27     | 2    | temperature          | i16  | 0.01 °C                |
| 29     | 1    | humidity             | u8   | %RH                    |
| 30     | 2    | ax                   | i16  | milli-g (m/s² ÷ 9.80665 × 1000) |
| 32     | 2    | ay                   | i16  | milli-g                |
| 34     | 2    | az                   | i16  | milli-g                |
| 36     | 2    | gx                   | i16  | 0.1 °/s                |
| 38     | 2    | gy                   | i16  | 0.1 °/s                |
| 40     | 2    | gz                   | i16  | 0.1 °/s                |
| 42     | 2    | geiger_cpm           | u16  | counts per minute      |
| 44     | 2    | geiger_window_counts | u16  | counts in window       |

## Type 2 — status text payload (bytes 11..45)

| Offset | Size | Field | Type | Notes                        |
|--------|------|-------|------|------------------------------|
| 11     | 1    | len   | u8   | text length (max 37)         |
| 12     | ≤37  | text  | char | ASCII status, e.g. `ARMED`   |
| ...    |      | pad   | —    | zero padding to byte 45      |

Status frames are sent on state transitions and flight events (ARMED,
LAUNCHED, LANDED, DISARMED). LAUNCHED is sent when the launch latch trips
(baro altitude gain or sustained high-g while ARMED); the state byte remains
ARMED until landing detection.

## Link budget / rates

48 B at the default 2400 bit/s air data rate ≈ 220 ms on-air per frame
(incl. module overhead), so the default 2 Hz telemetry rate keeps well under
typical duty-cycle limits. Raise `lora.air_baud` before raising
`rate.telemetry_hz` substantially.

## Optional RSSI byte

When `lora.rssi_append=true` on **both** modules, the receiving module
appends one RSSI byte after each packet. The ground station accepts this via
its `--rssi` flag; with it off, it resynchronises on the `FL` magic + CRC.

## LoRa module register notes (Waveshare / EBYTE E22 style)

The flight computer only writes module registers when `lora.configure=true`
(factory defaults usually already work). Register map used by
`src/telemetry/lora.cpp`:

- `REG0`: `[7:5]` UART baud (011 = 9600), `[4:3]` parity (00 = 8N1),
  `[2:0]` air data rate (010 = 2.4k)
- `REG1`: `[7:6]` sub-packet size (00 = 240 B), `[1:0]` TX power
  (00 = 22 dBm, 01 = 17, 10 = 13, 11 = 10)
- `REG2`: channel (868 MHz HAT: frequency ≈ 850.125 + channel MHz;
  channel 18 ≈ 868.125 MHz)
- `REG3`: `[6]` RSSI byte enable, `[5]` fixed-point transmission

**Verify this map against the Waveshare wiki for your HAT revision before
enabling `lora.configure`.**
