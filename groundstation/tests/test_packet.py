#!/usr/bin/env python3
"""Ground station packet tests, including the C++ <-> Python golden vector.

Run from the groundstation/ directory:
    python tests/test_packet.py

The golden frame below is emitted by the C++ encoder (tests/test_packet.cpp
prints the same hex) — if both agree, both sides of the radio link agree.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from firelink_gs import packet

# Golden vector: fixed field values, encoded by the C++ implementation.
GOLDEN_HEX_FILE = os.path.join(os.path.dirname(__file__), "golden_frame.hex")


def golden_fields():
    return {
        "state": 2, "t_ms": 123456,
        "lat": 55.8430123, "lon": -3.2400456, "gps_alt_m": 1234,
        "sats": 11, "fix_quality": 4, "speed_kmh": 321.5,
        "baro_alt_rel_m": 1180, "temperature_c": 21.34,
        "humidity_pct": 47,
        "ax_g": 0.111, "ay_g": -0.222, "az_g": 1.333,
        "gx_dps": -45.6, "gy_dps": 7.8, "gz_dps": 190.1,
        "geiger_cpm": 66, "geiger_window_counts": 42,
    }


def main():
    failures = 0

    def check(name, cond):
        nonlocal failures
        print(("PASS" if cond else "FAIL"), name)
        if not cond:
            failures += 1

    # CRC known vector: "123456789" -> 0x29B1 for CRC-16/CCITT-FALSE.
    check("crc16 known vector", packet.crc16_ccitt(b"123456789") == 0x29B1)

    # Encode/decode round trip.
    fields = golden_fields()
    frame = packet.encode_telemetry(1234, fields)
    check("frame size", len(frame) == packet.PACKET_SIZE)
    d = packet.decode(frame)
    check("decode returns dict", d is not None)
    check("seq", d["seq"] == 1234)
    check("state", d["state_name"] == "ARMED")
    check("lat close", abs(d["lat"] - fields["lat"]) < 1e-6)
    check("lon close", abs(d["lon"] - fields["lon"]) < 1e-6)
    check("gps_alt", d["gps_alt_m"] == 1234)
    check("sats", d["sats"] == 11)
    check("speed", abs(d["speed_kmh"] - 321.5) < 0.06)
    check("temp", abs(d["temperature_c"] - 21.34) < 0.005)
    check("az_g", abs(d["az_g"] - 1.333) < 0.001)
    check("gz_dps", abs(d["gz_dps"] - 190.1) < 0.06)
    check("cpm", d["geiger_cpm"] == 66)

    # Corrupt a byte -> CRC fails.
    bad = bytearray(frame)
    bad[20] ^= 0xFF
    check("crc rejects corruption", packet.decode(bytes(bad)) is None)

    # Status packet round trip.
    s = packet.encode_status(77, 2, 555, "LANDED")
    d = packet.decode(s)
    check("status decode", d is not None and d["text"] == "LANDED"
          and d["seq"] == 77)

    # Ground-station -> payload link report (type 3) round trip.
    lf = packet.encode_link_report(7, {
        "last_rx_seq": 1200, "packets_ok": 12345, "packets_lost": 3,
        "packets_bad": 1, "rssi_dbm": -78, "rate_cps": 2.03,
        "rx_age_ms": 500, "flags": 1,
    })
    check("link frame size", len(lf) == packet.PACKET_SIZE)
    dl = packet.decode_link_report(lf)
    check("link decode", dl is not None)
    check("link last_rx_seq", dl["last_rx_seq"] == 1200)
    check("link ok", dl["packets_ok"] == 12345)
    check("link lost", dl["packets_lost"] == 3)
    check("link rssi", dl["rssi_dbm"] == -78 and dl["rssi_dbm_valid"])
    check("link rate", abs(dl["rate_cps"] - 2.03) < 1e-9)
    check("link rx_age", dl["rx_age_ms"] == 500)
    badl = bytearray(lf)
    badl[20] ^= 0xFF
    check("link rejects corruption",
          packet.decode_link_report(bytes(badl)) is None)
    check("telemetry is not a link report",
          packet.decode_link_report(frame) is None)
    # A beacon with no RSSI is valid but flagged invalid.
    no_rssi = packet.encode_link_report(1, {"rssi_dbm": 0, "flags": 0})
    check("link no-rssi flagged",
          packet.decode_link_report(no_rssi)["rssi_dbm_valid"] is False)

    # Golden-file comparison against the C++ encoder (if present).
    if os.path.exists(GOLDEN_HEX_FILE):
        golden = bytes.fromhex(open(GOLDEN_HEX_FILE).read().strip())
        check("golden matches python encoder", golden == frame)
        d = packet.decode(golden)
        check("golden decodes", d is not None and d["lat"] > 55.84)
    else:
        print(f"SKIP golden vector (run 'make golden' to generate "
              f"{GOLDEN_HEX_FILE})")

    print(f"\n{'ALL PASS' if failures == 0 else f'{failures} FAILURES'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
