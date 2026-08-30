#include "tests.h"

#include "telemetry/packet.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <fstream>

static int g_failures = 0;

#define CHECK(name, cond)                                            \
    do {                                                             \
        bool ok_ = (cond);                                           \
        std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", name);         \
        if (!ok_) ++g_failures;                                      \
    } while (0)

// These literal fields must match golden_fields() in
// groundstation/tests/test_packet.py exactly.
static TelemetryFields goldenFields() {
    TelemetryFields f;
    f.state = 2;
    f.t_ms = 123456;
    f.lat_e7 = (int32_t)std::llround(55.8430123 * 1e7);
    f.lon_e7 = (int32_t)std::llround(-3.2400456 * 1e7);
    f.gps_alt_m = 1234;
    f.sats = 11;
    f.fix_quality = 4;
    f.speed_dkmh = (uint16_t)std::llround(321.5 * 10);
    f.baro_alt_rel_m = 1180;
    f.temp_c100 = (int16_t)std::llround(21.34 * 100);
    f.humidity_pct = 47;
    f.ax_mg = (int16_t)std::llround(0.111 * 1000);
    f.ay_mg = (int16_t)std::llround(-0.222 * 1000);
    f.az_mg = (int16_t)std::llround(1.333 * 1000);
    f.gx_dps10 = (int16_t)std::llround(-45.6 * 10);
    f.gy_dps10 = (int16_t)std::llround(7.8 * 10);
    f.gz_dps10 = (int16_t)std::llround(190.1 * 10);
    f.geiger_cpm = 66;
    f.geiger_window = 42;
    return f;
}

int testPacket(const char *golden_path) {
    g_failures = 0;

    // CRC-16/CCITT-FALSE known vector.
    const uint8_t vec[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK("crc16 known vector", crc16Ccitt(vec, 9) == 0x29B1);

    // Encode/decode round trip.
    TelemetryFields out_fields = goldenFields();
    uint8_t frame[FIRELINK_PACKET_SIZE];
    size_t n = packetEncodeTelemetry(1234, out_fields, frame);
    CHECK("frame size", n == FIRELINK_PACKET_SIZE);
    CHECK("magic", frame[0] == 'F' && frame[1] == 'L');

    uint16_t seq = 0;
    uint8_t type = 0;
    TelemetryFields in_fields;
    std::string status;
    CHECK("decode ok",
          packetDecode(frame, n, seq, type, in_fields, status));
    CHECK("seq", seq == 1234);
    CHECK("type", type == FIRELINK_TYPE_TELEMETRY);
    CHECK("state", in_fields.state == 2);
    CHECK("t_ms", in_fields.t_ms == 123456);
    CHECK("lat", in_fields.lat_e7 == out_fields.lat_e7);
    CHECK("lon", in_fields.lon_e7 == out_fields.lon_e7);
    CHECK("gps_alt", in_fields.gps_alt_m == 1234);
    CHECK("sats", in_fields.sats == 11);
    CHECK("speed", in_fields.speed_dkmh == out_fields.speed_dkmh);
    CHECK("baro", in_fields.baro_alt_rel_m == 1180);
    CHECK("temp", in_fields.temp_c100 == out_fields.temp_c100);
    CHECK("accel", in_fields.ay_mg == out_fields.ay_mg);
    CHECK("gyro", in_fields.gz_dps10 == out_fields.gz_dps10);
    CHECK("geiger", in_fields.geiger_cpm == 66 && in_fields.geiger_window == 42);

    // Corruption is rejected.
    uint8_t corrupt[FIRELINK_PACKET_SIZE];
    std::memcpy(corrupt, frame, sizeof(corrupt));
    corrupt[20] ^= 0xFF;
    CHECK("crc rejects corruption",
          !packetDecode(corrupt, n, seq, type, in_fields, status));

    // Status packet round trip.
    uint8_t sframe[FIRELINK_PACKET_SIZE];
    size_t sn = packetEncodeStatus(77, 3, 555, "LANDED", sframe);
    CHECK("status encode size", sn == FIRELINK_PACKET_SIZE);
    status.clear();
    CHECK("status decode",
          packetDecode(sframe, sn, seq, type, in_fields, status));
    CHECK("status text", status == "LANDED");
    CHECK("status seq/type", seq == 77 && type == FIRELINK_TYPE_STATUS);
    CHECK("status state", in_fields.state == 3);

    // Snapshot conversion incl. clamping.
    Snapshot snap;
    snap.state = FlightState::ARMED;
    snap.gps_lat = 55.8430123;
    snap.gps_lon = -3.2400456;
    snap.ax = 2000.0f; // >32767 milli-g -> clamps to int16 range
    TelemetryFields conv = telemetryFieldsFromSnapshot(snap);
    CHECK("snapshot lat", conv.lat_e7 == (int32_t)std::llround(55.8430123 * 1e7));
    CHECK("snapshot clamp", conv.ax_mg == 32767);

    if (golden_path) {
        std::ofstream f(golden_path, std::ios::trunc);
        static const char *hex = "0123456789abcdef";
        for (size_t i = 0; i < n; ++i) {
            f << hex[frame[i] >> 4] << hex[frame[i] & 0xF];
        }
        f << "\n";
        std::printf("golden frame written to %s\n", golden_path);
    }

    return g_failures;
}
