"""Faithful model of the flight-viewer JS ingest logic.

Mirrors src/web/flight_page.cpp accessor-for-accessor (same field names,
same guards, same order) so a rename mismatch in the real CSVs shows up as
an empty series here. Run: python tools/verify_viewer_model.py
"""

import csv
import datetime
import math
import re
import sys
from pathlib import Path

G = 9.80665
DATA = Path(sys.argv[1] if len(sys.argv) > 1 else "20260904T010051")


def to_objects(rows):
    if not rows:
        return []
    head = [h.strip() for h in rows[0]]
    return [dict(zip(head, r)) for r in rows[1:]]


def num(v):
    try:
        x = float(v)
        return x if math.isfinite(x) else float("nan")
    except (TypeError, ValueError):
        return float("nan")


def parse_time(s):
    if not s:
        return float("nan")
    s = s.strip()
    m = re.match(r"^(\d{1,2})/(\d{1,2})/(\d{4})\s+(\d{1,2}):(\d{2}):(\d{2})", s)
    if m:
        d = datetime.datetime(int(m[3]), int(m[2]), int(m[1]), int(m[4]),
                              int(m[5]), int(m[6]))
        return d.timestamp()
    m = re.match(r"^(\d{4})-(\d{2})-(\d{2})[T ](\d{1,2}):(\d{2}):(\d{2})", s)
    if m:
        d = datetime.datetime(int(m[1]), int(m[2]), int(m[3]), int(m[4]),
                              int(m[5]), int(m[6]))
        return d.timestamp()
    return float("nan")


def read_csv(path):
    with path.open(newline="", encoding="utf-8", errors="replace") as f:
        return list(csv.reader(f))


def ingest_baro(rows):
    t, alt, p, temp, hum = [], [], [], [], []
    for r in rows:
        e = parse_time(r.get("timestamp") or r.get("rx_time"))
        if math.isfinite(e) and math.isfinite(num(r.get("relative_altitude", r.get("baro_alt_rel_m")))):
            t.append(e); alt.append(num(r.get("relative_altitude", r.get("baro_alt_rel_m"))))
        if r.get("pressure_hpa") is not None:
            p.append(num(r["pressure_hpa"]))
        if r.get("temperature_c") is not None:
            temp.append(num(r["temperature_c"]))
        if r.get("humidity_pct") is not None:
            hum.append(num(r["humidity_pct"]))
    return t, alt, p, temp, hum


def ingest_accel(rows, g_mode):
    t, ax, ay, az = [], [], [], []
    g = G if g_mode else 1.0
    for r in rows:
        e = parse_time(r.get("timestamp") or r.get("rx_time"))
        if not math.isfinite(e):
            continue
        t.append(e)
        ax.append(num(r.get("accel_x", r.get("ax_g"))) * g)
        ay.append(num(r.get("accel_y", r.get("ay_g"))) * g)
        az.append(num(r.get("accel_z", r.get("az_g"))) * g)
    return t, ax, ay, az


def ingest_gps(rows):
    t, lat, lon, alt, sats = [], [], [], [], []
    for r in rows:
        e = parse_time(r.get("thread_datetime") or r.get("datetime")
                       or r.get("rx_time") or r.get("timestamp"))
        if not math.isfinite(e):
            continue
        la = num(r.get("latitude", r.get("lat")))
        lo = num(r.get("longitude", r.get("lon")))
        if not math.isfinite(la) or not math.isfinite(lo):
            continue
        sat = num(r.get("satellites", r.get("sats", 0)))
        fix = (num(r.get("fix")) > 0) if r.get("fix") is not None else (sat > 0)
        if not fix or abs(la) > 90 or abs(lo) > 180:
            continue
        t.append(e); lat.append(la); lon.append(lo)
        alt.append(num(r.get("altitude_m", r.get("gps_alt_m"))))
        sats.append(sat)
    return t, lat, lon, alt, sats


def ingest_geiger(rows):
    t, cpm = [], []
    for r in rows:
        if r.get("cpm") is None:
            continue
        e = parse_time(r.get("timestamp") or r.get("rx_time"))
        if math.isfinite(e) and math.isfinite(num(r["cpm"])):
            t.append(e); cpm.append(num(r["cpm"]))
    return t, cpm


def ingest_states(rows):
    states, last = [], None
    for r in rows:
        name = (r.get("state_name") or "").strip()
        if not name or name == last:
            last = name
            continue
        last = name
        e = parse_time(r.get("rx_time") or r.get("timestamp"))
        if math.isfinite(e):
            states.append((e, name))
    return states


def ingest_events(text):
    events, states, pad = [], [], None
    state_re = re.compile(r"^state:\s*(\w+)\s*->\s*(\w+)(?:\s*\((.+)\))?$")
    pad_re = re.compile(r"^PAD pressure recalibrated to ([\d.]+) hPa")
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r"^(\d{1,2}/\d{1,2}/\d{4} \d{1,2}:\d{2}:\d{2}(?:\.\d{1,3})?)\s+(.*)$", line)
        if not m:
            continue
        e = parse_time(m.group(1))
        if not math.isfinite(e):
            continue
        msg = m.group(2)
        st = state_re.match(msg)
        if st:
            states.append((e, st.group(1), st.group(2), st.group(3) or ""))
        events.append((e, msg))
        pm = pad_re.match(msg)
        if pm:
            pad = float(pm.group(1))
    return events, states, pad


def derived_velocity(t, ax, ay, az):
    if len(t) < 4:
        return [], []
    n = min(50, len(t))
    sx = sum(ax[:n]); sy = sum(ay[:n]); sz = sum(az[:n])
    mm = math.hypot(sx, sy, sz) / n or G
    ux, uy, uz = sx / (mm * n), sy / (mm * n), sz / (mm * n)
    vt, out, load = [], [], []
    v = 0.0
    for i in range(len(t)):
        loadv = ax[i] * ux + ay[i] * uy + az[i] * uz
        a_vert = loadv - G
        if i > 0:
            dt = t[i] - t[i - 1]
            if dt > 1e-4:
                v += a_vert * dt
        vt.append(t[i]); out.append(v); load.append(loadv / G)
    return vt, out


def summary(label, d):
    print(f"== {label}")
    for k, v in d.items():
        if v is None:
            print(f"  {k}: -")
        elif isinstance(v, list):
            print(f"  {k}: {len(v)} points" +
                  (f", first={v[0]:.4f}" if v and isinstance(v[0], float) else ""))
        elif isinstance(v, (int, float)):
            print(f"  {k}: {v:.2f}" if isinstance(v, float) else f"  {k}: {v}")
        else:
            print(f"  {k}: {v}")


def main():
    baro = to_objects(read_csv(DATA / "barometer.csv"))
    accel = to_objects(read_csv(DATA / "accelerometer.csv"))
    gps = to_objects(read_csv(DATA / "gps.csv"))
    gei = to_objects(read_csv(DATA / "geiger.csv"))
    ev = (DATA / "events.log").read_text(encoding="utf-8", errors="replace")
    tb, alt, pb, temp, hum = ingest_baro(baro)
    ta, ax, ay, az = ingest_accel(accel, False)
    tg, la, lo, ga, sa = ingest_gps(gps)
    tge, cpm = ingest_geiger(gei)
    events, states, pad = ingest_events(ev)
    vt, vel = derived_velocity(ta, ax, ay, az)
    summary("PI session (20260904T010051)", {
        "baro rows": len(baro), "baro alt pts": len(alt),
        "accel rows": len(accel), "accel pts": len(ta),
        "gps rows": len(gps), "gps pts": len(tg),
        "geiger pts": len(tge), "cpm": cpm,
        "events": len(events), "state transitions": len(states), "pad_hpa": pad,
        "max baro alt": max(alt) if alt else None,
        "max |a| g": (max(math.hypot(ax[i], ay[i], az[i]) for i in range(len(ta))) / G) if ta else None,
        "max derived v": max(vel) if vel else None,
    })
    assert len(tb) == len(baro) and len(alt) == len(baro), "baro empty (field/time mismatch?)"
    assert ta, "accel empty"
    assert len(events) >= 3, "events not parsed"
    # If a session captured GPS fixes, they must not be silently dropped:
    # Pi gps.csv's wall clock lives in thread_datetime (datetime is the raw
    # NMEA HHMMSS), so an empty track means the timestamp field drifted.
    if len(gps) > 1:
        assert tg, "gps.csv has rows but 0 points ingested (timestamp field mismatch?)"
    print("  state lines seen:", [f"{a}->{b}" for _, a, b, _ in states])
    print("  first baro alt:", alt[0], " max baro alt:", max(alt))
    print("  pad pressure from events:", pad)

    # GS-mode telemetry.csv: build from receiver.py schema, verify lat/lon + g-mode.
    gs = Path(DATA) / "gs_telemetry.csv"
    with gs.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["rx_time", "seq", "state_name", "t_ms", "lat", "lon", "gps_alt_m",
                    "sats", "fix_quality", "speed_kmh", "baro_alt_rel_m",
                    "temperature_c", "humidity_pct", "ax_g", "ay_g", "az_g"])
        for i in range(20):
            st = "ARMED" if i < 12 else "LANDED"
            w.writerow([f"2026-09-05T12:00:{i:02d}", i, st, i * 1000,
                        55.0 + i * 1e-4, -3.0 + i * 1e-4, 12.5 + i, 9 + (i % 3),
                        1, 55.0, 0.1 + i, 25.5, 45.0, 0.01, 0.02, 1.005])
    brows = to_objects(read_csv(gs))
    tb2, alt2, _, _, _ = ingest_baro(brows)
    ta2, ax2, ay2, az2 = ingest_accel(brows, True)
    tg2, la2, lo2, ga2, sa2 = ingest_gps(brows)
    st2 = ingest_states(brows)
    summary("GS mode (synthetic telemetry.csv)", {
        "rows": len(brows), "baro pts": len(tb2), "accel pts": len(ta2),
        "gps pts": len(tg2), "states": len(st2),
        "first gps": f"{la2[0]:.6f},{lo2[0]:.6f}" if tg2 else "EMPTY",
    })
    assert len(tg2) == 20, "GS lat/lon mapping broken"
    assert len(st2) >= 2, "GS state transitions broken"
    assert any(abs(math.hypot(ax2[i], ay2[i], az2[i]) / G - 1.0) < 0.05 for i in range(len(ta2))), \
        "g-mode scaling broken"
    print("\nALL CHECKS PASS")


if __name__ == "__main__":
    main()