#!/usr/bin/env python3
"""Fake Firelink transmitter for testing the ground station without hardware.

Default mode creates a virtual serial port and streams a synthetic flight
(preflight -> armed ascent/descent -> landed beacon):

    python tools/fake_transmitter.py
    # prints:  pty slave is at /dev/ttys012
    python -m firelink_gs --port /dev/ttys012 --web

Or write a replay file instead:

    python tools/fake_transmitter.py --hex flight.hex
    python -m firelink_gs --replay flight.hex
"""

import argparse
import math
import os
import pty
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from firelink_gs import packet


def flight_frames(base_lat, base_lon, rate_hz, arm_s, flight_s, beacon_s):
    """Yield (delay_s, frame_bytes) for a synthetic flight."""
    seq = 0
    dt = 1.0 / rate_hz
    frames = []

    def add_telemetry(state, t_ms, fields, delay):
        nonlocal seq
        f = dict(fields)
        f["state"] = state
        f["t_ms"] = t_ms
        frames.append((delay, packet.encode_telemetry(seq, f)))
        seq = (seq + 1) & 0xFFFF

    def add_status(state, t_ms, text, delay):
        nonlocal seq
        frames.append((delay, packet.encode_status(seq, state, t_ms, text)))
        seq = (seq + 1) & 0xFFFF

    common = {
        "lat": base_lat, "lon": base_lon, "gps_alt_m": 40, "sats": 9,
        "fix_quality": 1, "speed_kmh": 0.0, "baro_alt_rel_m": 0,
        "temperature_c": 18.0, "humidity_pct": 55, "ax_g": 0.0,
        "ay_g": 0.0, "az_g": 1.0, "gx_dps": 0.0, "gy_dps": 0.0,
        "gz_dps": 0.0, "geiger_cpm": 22, "geiger_window_counts": 22,
    }

    # Pre-flight on the pad.
    t = 0.0
    while t < arm_s:
        add_telemetry(1, 0, common, dt)
        t += dt

    add_status(2, 0, "ARMED", dt)

    # Flight: fast ascent, apogee, descent, touchdown.
    t = 0.0
    ascent_s = flight_s * 0.35
    peak = 3000.0
    while t < flight_s:
        if t < ascent_s:
            frac = t / ascent_s
            alt = peak * (1 - math.cos(math.pi * frac)) / 2  # smooth ascent
            az = 1.0 + 6.0 * math.sin(math.pi * min(frac * 2, 1))
        else:
            frac = (t - ascent_s) / max(flight_s - ascent_s, 0.001)
            alt = max(peak * (1 - frac * frac), 0.0)
            az = 1.0 if alt <= 1.0 else -0.5
        moving = {
            **common,
            "lat": base_lat + 0.004 * (t / flight_s),
            "lon": base_lon + 0.006 * math.sin(3 * t / flight_s),
            "gps_alt_m": 40 + int(alt),
            "baro_alt_rel_m": int(alt),
            "speed_kmh": 300.0 if 0 < alt < peak - 1 else 2.0,
            "az_g": az,
        }
        add_telemetry(2, int(t * 1000), moving, dt)
        t += dt

    add_status(3, int(t * 1000), "LANDED", dt)

    # Landed beacon: fixed position, slow rate.
    beacon_dt = max(5.0, dt)
    t = 0.0
    landed = {**common, "lat": base_lat + 0.004, "lon": base_lon}
    while t < beacon_s:
        add_telemetry(3, 0, landed, beacon_dt)
        t += beacon_dt

    return frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lat", type=float, default=55.8430,
                    help="base latitude (default: near Edinburgh)")
    ap.add_argument("--lon", type=float, default=-3.2400)
    ap.add_argument("--rate", type=float, default=2.0, help="frames per second")
    ap.add_argument("--arm-s", type=float, default=6.0)
    ap.add_argument("--flight-s", type=float, default=40.0)
    ap.add_argument("--beacon-s", type=float, default=20.0)
    ap.add_argument("--hex", metavar="FILE",
                    help="write a hex replay file instead of streaming a pty")
    args = ap.parse_args()

    frames = flight_frames(args.lat, args.lon, args.rate,
                           args.arm_s, args.flight_s, args.beacon_s)

    if args.hex:
        with open(args.hex, "w") as f:
            for _, frame in frames:
                f.write(frame.hex() + "\n")
        print(f"Wrote {len(frames)} frames to {args.hex}")
        return

    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)
    print(f"PTY {slave_name}", flush=True)
    print(f"Streaming synthetic flight on: {slave_name}", flush=True)
    print(f"Run:  python -m firelink_gs --port {slave_name} --web", flush=True)
    print("Ctrl+C to stop.", flush=True)
    try:
        for delay, frame in frames:
            time.sleep(delay)
            os.write(master, frame)
        print("Flight complete (pty stays open; Ctrl+C to exit).")
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
