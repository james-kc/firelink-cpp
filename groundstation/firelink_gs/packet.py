"""Firelink telemetry packet codec.

Mirror of include/telemetry/packet.h / src/telemetry/packet.cpp in the
flight computer. Both sides of the link must match byte-for-byte; the
byte-level contract is documented in docs/telemetry_protocol.md.
"""

import math
import struct

PACKET_SIZE = 48
VERSION = 1
TYPE_TELEMETRY = 1
TYPE_STATUS = 2
MAGIC = b"FL"

STATE_NAMES = {0: "BOOT", 1: "PREFLIGHT", 2: "ARMED", 3: "LANDED"}

# magic, version, type, seq, state, t_ms,
# lat, lon, gps_alt, sats, fixq, speed, baro_alt, temp, hum,
# ax..az, gx..gz, cpm, window_counts, crc16
FRAME = struct.Struct("<2sBBHBIiihBBHhhB6hHHH")
assert FRAME.size == PACKET_SIZE, FRAME.size


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def decode(frame: bytes):
    """Decode a 48-byte frame.

    Returns a dict for telemetry packets:
        type, seq, state, state_name, t_ms, lat, lon, gps_alt_m, sats,
        fix_quality, speed_kmh, baro_alt_rel_m, temperature_c, humidity_pct,
        ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps, geiger_cpm,
        geiger_window_counts
    or for status packets: type, seq, state, state_name, t_ms, text.
    Returns None if the frame is malformed or fails its CRC.
    """
    if len(frame) < PACKET_SIZE:
        return None

    try:
        (magic, version, ptype, seq, state, t_ms,
         lat_e7, lon_e7, gps_alt, sats, fixq, speed_dkmh,
         baro_alt, temp_c100, hum,
         ax, ay, az, gx, gy, gz,
         cpm, window, crc) = FRAME.unpack(frame[:PACKET_SIZE])
    except struct.error:
        return None

    if magic != MAGIC or version != VERSION:
        return None
    if crc != crc16_ccitt(frame[:PACKET_SIZE - 2]):
        return None

    base = {
        "type": ptype,
        "seq": seq,
        "state": state,
        "state_name": STATE_NAMES.get(state, "?"),
        "t_ms": t_ms,
    }

    if ptype == TYPE_STATUS:
        n = frame[11]
        if n > 37:
            return None
        base["text"] = frame[12:12 + n].decode("ascii", errors="replace")
        return base

    if ptype != TYPE_TELEMETRY:
        return None

    base.update({
        "lat": lat_e7 / 1e7,
        "lon": lon_e7 / 1e7,
        "gps_alt_m": gps_alt,
        "sats": sats,
        "fix_quality": fixq,
        "speed_kmh": speed_dkmh / 10.0,
        "baro_alt_rel_m": baro_alt,
        "temperature_c": temp_c100 / 100.0,
        "humidity_pct": hum,
        "ax_g": ax / 1000.0,
        "ay_g": ay / 1000.0,
        "az_g": az / 1000.0,
        "gx_dps": gx / 10.0,
        "gy_dps": gy / 10.0,
        "gz_dps": gz / 10.0,
        "geiger_cpm": cpm,
        "geiger_window_counts": window,
    })
    return base


def _iround(x: float) -> int:
    """Round-half-away-from-zero, matching C++ llround in the encoder."""
    return int(math.floor(x + 0.5)) if x >= 0 else int(math.ceil(x - 0.5))


def encode_telemetry(seq: int, fields: dict) -> bytes:
    """Encode a telemetry frame (used by tools/fake_transmitter.py).

    `fields` uses the same unit-bearing keys produced by decode().
    """
    header = struct.pack(
        "<2sBBHBI",
        MAGIC, VERSION, TYPE_TELEMETRY, seq & 0xFFFF,
        fields.get("state", 1), fields.get("t_ms", 0) & 0xFFFFFFFF,
    )
    payload = struct.pack(
        "<iihBBHhhB6hHH",
        _iround(fields.get("lat", 0.0) * 1e7),
        _iround(fields.get("lon", 0.0) * 1e7),
        _iround(fields.get("gps_alt_m", 0)),
        _iround(fields.get("sats", 0)),
        _iround(fields.get("fix_quality", 0)),
        _iround(fields.get("speed_kmh", 0.0) * 10),
        _iround(fields.get("baro_alt_rel_m", 0)),
        _iround(fields.get("temperature_c", 0.0) * 100),
        _iround(fields.get("humidity_pct", 0)),
        _iround(fields.get("ax_g", 0.0) * 1000),
        _iround(fields.get("ay_g", 0.0) * 1000),
        _iround(fields.get("az_g", 0.0) * 1000),
        _iround(fields.get("gx_dps", 0.0) * 10),
        _iround(fields.get("gy_dps", 0.0) * 10),
        _iround(fields.get("gz_dps", 0.0) * 10),
        _iround(fields.get("geiger_cpm", 0)),
        _iround(fields.get("geiger_window_counts", 0)),
    )
    body = header + payload
    assert len(body) == PACKET_SIZE - 2, len(body)
    return body + struct.pack("<H", crc16_ccitt(body))


def encode_status(seq: int, state: int, t_ms: int, text: str) -> bytes:
    """Encode a status-text frame (max 37 ASCII chars)."""
    raw = text.encode("ascii", errors="replace")[:37]
    header = struct.pack(
        "<2sBBHBI", MAGIC, VERSION, TYPE_STATUS, seq & 0xFFFF,
        state, t_ms & 0xFFFFFFFF,
    )
    payload = struct.pack("<B", len(raw)) + raw
    payload = payload.ljust(PACKET_SIZE - 2 - len(header), b"\x00")
    body = header + payload
    assert len(body) == PACKET_SIZE - 2, len(body)
    return body + struct.pack("<H", crc16_ccitt(body))
