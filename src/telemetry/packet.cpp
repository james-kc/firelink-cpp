#include "telemetry/packet.h"

#include <cmath>
#include <algorithm>

uint16_t crc16Ccitt(const uint8_t *data, size_t len) {
    // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF.
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

static inline void putU16(uint8_t *p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
}

static inline void putU32(uint8_t *p, uint32_t v) {
    p[0] = v & 0xFF;
    p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 16) & 0xFF;
    p[3] = (v >> 24) & 0xFF;
}

static inline uint16_t getU16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t getU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int16_t clampI16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)std::lround(v);
}

static uint16_t clampU16(float v) {
    if (v > 65535.0f) return 65535;
    if (v < 0.0f) return 0;
    return (uint16_t)std::lround(v);
}

TelemetryFields telemetryFieldsFromSnapshot(const Snapshot &s) {
    TelemetryFields f;
    f.state = (uint8_t)s.state;
    f.t_ms = (s.arm_time_ms > 0) ? (uint32_t)(steadyMs() - s.arm_time_ms) : 0;
    f.lat_e7 = (int32_t)std::llround(s.gps_lat * 1e7);
    f.lon_e7 = (int32_t)std::llround(s.gps_lon * 1e7);
    f.gps_alt_m = clampI16(s.gps_alt_m);
    f.sats = (uint8_t)std::min(std::max(s.gps_sats, 0), 255);
    f.fix_quality = (uint8_t)std::min(std::max(s.gps_fix_quality, 0), 255);
    f.speed_dkmh = clampU16(s.gps_speed_kmh * 10.0f);
    f.baro_alt_rel_m = clampI16(s.baro_alt_rel_m);
    f.temp_c100 = clampI16(s.temperature_c * 100.0f);
    f.humidity_pct = (uint8_t)std::min(std::max((int)std::lround(s.humidity_pct), 0), 255);
    f.ax_mg = clampI16(s.ax / 9.80665f * 1000.0f);
    f.ay_mg = clampI16(s.ay / 9.80665f * 1000.0f);
    f.az_mg = clampI16(s.az / 9.80665f * 1000.0f);
    f.gx_dps10 = clampI16(s.gx * 10.0f);
    f.gy_dps10 = clampI16(s.gy * 10.0f);
    f.gz_dps10 = clampI16(s.gz * 10.0f);
    f.geiger_cpm = (uint16_t)std::min(s.geiger_cpm, (uint32_t)65535);
    f.geiger_window = (uint16_t)std::min(s.geiger_window_counts, (uint32_t)65535);
    return f;
}

static size_t finishFrame(uint8_t out[FIRELINK_PACKET_SIZE]) {
    uint16_t crc = crc16Ccitt(out, FIRELINK_PACKET_SIZE - 2);
    putU16(out + FIRELINK_PACKET_SIZE - 2, crc);
    return FIRELINK_PACKET_SIZE;
}

static void writeHeader(uint8_t *out, uint8_t type, uint16_t seq, uint8_t state) {
    out[0] = 'F';
    out[1] = 'L';
    out[2] = FIRELINK_PACKET_VERSION;
    out[3] = type;
    putU16(out + 4, seq);
    out[6] = state;
}

size_t packetEncodeTelemetry(uint16_t seq, const TelemetryFields &f,
                             uint8_t out[FIRELINK_PACKET_SIZE]) {
    writeHeader(out, FIRELINK_TYPE_TELEMETRY, seq, f.state);
    putU32(out + 7, f.t_ms);
    putU32(out + 11, (uint32_t)f.lat_e7);
    putU32(out + 15, (uint32_t)f.lon_e7);
    putU16(out + 19, (uint16_t)f.gps_alt_m);
    out[21] = f.sats;
    out[22] = f.fix_quality;
    putU16(out + 23, f.speed_dkmh);
    putU16(out + 25, (uint16_t)f.baro_alt_rel_m);
    putU16(out + 27, (uint16_t)f.temp_c100);
    out[29] = f.humidity_pct;
    putU16(out + 30, (uint16_t)f.ax_mg);
    putU16(out + 32, (uint16_t)f.ay_mg);
    putU16(out + 34, (uint16_t)f.az_mg);
    putU16(out + 36, (uint16_t)f.gx_dps10);
    putU16(out + 38, (uint16_t)f.gy_dps10);
    putU16(out + 40, (uint16_t)f.gz_dps10);
    putU16(out + 42, f.geiger_cpm);
    putU16(out + 44, f.geiger_window);
    return finishFrame(out);
}

size_t packetEncodeStatus(uint16_t seq, uint8_t state, uint32_t t_ms,
                          const std::string &text,
                          uint8_t out[FIRELINK_PACKET_SIZE]) {
    writeHeader(out, FIRELINK_TYPE_STATUS, seq, state);
    putU32(out + 7, t_ms);
    size_t n = std::min(text.size(), (size_t)FIRELINK_STATUS_TEXT_MAX);
    out[11] = (uint8_t)n;
    for (size_t i = 0; i < n; ++i) out[12 + i] = (uint8_t)text[i];
    for (size_t i = 12 + n; i < FIRELINK_PACKET_SIZE - 2; ++i) out[i] = 0;
    return finishFrame(out);
}

bool packetDecode(const uint8_t *buf, size_t len, uint16_t &seq_out,
                  uint8_t &type_out, TelemetryFields &f,
                  std::string &status_text_out) {
    if (len < FIRELINK_PACKET_SIZE) return false;
    if (buf[0] != 'F' || buf[1] != 'L') return false;
    if (buf[2] != FIRELINK_PACKET_VERSION) return false;

    uint16_t crc = getU16(buf + FIRELINK_PACKET_SIZE - 2);
    if (crc != crc16Ccitt(buf, FIRELINK_PACKET_SIZE - 2)) return false;

    uint8_t type = buf[3];
    seq_out = getU16(buf + 4);
    f.state = buf[6];
    f.t_ms = getU32(buf + 7);
    type_out = type;

    if (type == FIRELINK_TYPE_STATUS) {
        uint8_t n = buf[11];
        if (n > FIRELINK_STATUS_TEXT_MAX) return false;
        status_text_out.assign((const char *)buf + 12, n);
        return true;
    }

    if (type != FIRELINK_TYPE_TELEMETRY) return false;

    f.lat_e7 = (int32_t)getU32(buf + 11);
    f.lon_e7 = (int32_t)getU32(buf + 15);
    f.gps_alt_m = (int16_t)getU16(buf + 19);
    f.sats = buf[21];
    f.fix_quality = buf[22];
    f.speed_dkmh = getU16(buf + 23);
    f.baro_alt_rel_m = (int16_t)getU16(buf + 25);
    f.temp_c100 = (int16_t)getU16(buf + 27);
    f.humidity_pct = buf[29];
    f.ax_mg = (int16_t)getU16(buf + 30);
    f.ay_mg = (int16_t)getU16(buf + 32);
    f.az_mg = (int16_t)getU16(buf + 34);
    f.gx_dps10 = (int16_t)getU16(buf + 36);
    f.gy_dps10 = (int16_t)getU16(buf + 38);
    f.gz_dps10 = (int16_t)getU16(buf + 40);
    f.geiger_cpm = getU16(buf + 42);
    f.geiger_window = getU16(buf + 44);
    return true;
}
