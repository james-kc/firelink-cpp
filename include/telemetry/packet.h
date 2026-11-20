#ifndef PACKET_H
#define PACKET_H

#include <cstdint>
#include <cstddef>
#include <string>
#include "state.h"

// Firelink telemetry packet — the contract between the flight computer and
// the ground station. See docs/telemetry_protocol.md for the byte-level
// specification and the Python mirror in groundstation/firelink_gs/packet.py.
//
// Fixed 48-byte frame, little-endian, matched by Python struct format:
//   magic[2] 'F','L' | version u8 | type u8 | seq u16 | state u8 |
//   t_ms u32 | lat i32 (1e-7 deg) | lon i32 (1e-7 deg) |
//   gps_alt i16 m | sats u8 | fix_quality u8 | speed u16 (0.1 km/h) |
//   baro_alt_rel i16 m | temp i16 (0.01 C) | humidity u8 % |
//   ax,ay,az i16 (milli-g) | gx,gy,gz i16 (0.1 dps) |
//   geiger_cpm u16 | geiger_window_counts u16 | crc16 u16
//
// Type 2 (status text) packets reuse the same 48-byte frame: bytes 7..46
// carry a length byte followed by up to 37 bytes of ASCII text.

#define FIRELINK_PACKET_SIZE      48
#define FIRELINK_PACKET_VERSION   1
#define FIRELINK_TYPE_TELEMETRY   1
#define FIRELINK_TYPE_STATUS      2
#define FIRELINK_TYPE_LINK        3   // ground-station -> payload link report
#define FIRELINK_STATUS_TEXT_MAX  37

uint16_t crc16Ccitt(const uint8_t *data, size_t len);

struct TelemetryFields {
    uint8_t  state = 1;
    uint32_t t_ms = 0;
    int32_t  lat_e7 = 0;
    int32_t  lon_e7 = 0;
    int16_t  gps_alt_m = 0;
    uint8_t  sats = 0;
    uint8_t  fix_quality = 0;
    uint16_t speed_dkmh = 0;      // 0.1 km/h
    int16_t  baro_alt_rel_m = 0;
    int16_t  temp_c100 = 0;       // 0.01 deg C
    uint8_t  humidity_pct = 0;
    int16_t  ax_mg = 0;           // milli-g (m/s^2 / 9.80665 * 1000)
    int16_t  ay_mg = 0;
    int16_t  az_mg = 0;
    int16_t  gx_dps10 = 0;        // 0.1 deg/s
    int16_t  gy_dps10 = 0;
    int16_t  gz_dps10 = 0;
    uint16_t geiger_cpm = 0;
    uint16_t geiger_window = 0;
};

// Clamp/convert a shared-state Snapshot into wire fields.
TelemetryFields telemetryFieldsFromSnapshot(const Snapshot &snap);

// Type 3 (link report) body, matched by the Python struct in packet.py:
//   <2sBBHBIHIHHbHIB17x
// Bytes 11..28 carry the ground-station's view of the payload link.
struct LinkReportFields {
    uint16_t last_rx_seq = 0;    // last payload telemetry sequence seen
    uint32_t packets_ok = 0;     // payload frames accepted (valid for log loop)
    uint16_t packets_lost = 0;   // gaps in payload sequence
    uint16_t packets_bad = 0;    // magic/CRC failures while in a frame
    int8_t   rssi_dbm = 0;       // signed dBm; valid only if flags bit 0 set
    uint16_t rate_cps_x100 = 0;  // payload frame rate, frames/s * 100
    uint32_t rx_age_ms = 0;      // ms since the last valid payload frame
    uint8_t  flags = 0;          // bit 0 = rssi_dbm valid
};

// Encode into a caller-provided 48-byte buffer. Returns FIRELINK_PACKET_SIZE.
size_t packetEncodeTelemetry(uint16_t seq, const TelemetryFields &f, uint8_t out[FIRELINK_PACKET_SIZE]);
size_t packetEncodeStatus(uint16_t seq, uint8_t state, uint32_t t_ms,
                          const std::string &text, uint8_t out[FIRELINK_PACKET_SIZE]);
size_t packetEncodeLink(uint16_t seq, const LinkReportFields &f,
                        uint8_t out[FIRELINK_PACKET_SIZE]);

// Validate + decode. Returns false on bad magic / length / CRC.
bool packetDecode(const uint8_t *buf, size_t len, uint16_t &seq_out,
                  uint8_t &type_out, TelemetryFields &fields_out,
                  std::string &status_text_out);

// Validate + decode a type-3 link report (buf must be at least the full
// 48-byte frame, validated magic/version/type/CRC).
bool packetDecodeLink(const uint8_t *buf, size_t len, LinkReportFields &f);

#endif // PACKET_H
