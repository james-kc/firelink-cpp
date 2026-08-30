"""Serial receiver: byte stream -> synchronised, CRC-checked packets.

Also owns the shared TelemetryStore that the dashboard and web views read.
A CSV of every telemetry packet received is written into the log directory.
"""

import csv
import os
import threading
import time
from collections import deque

from . import packet

try:
    import serial  # pyserial
except ImportError:  # replay-only operation
    serial = None


class TelemetryStore:
    """Thread-safe latest-values store shared by dashboard and web UI."""

    def __init__(self):
        self._lock = threading.Lock()
        self.latest = None          # last decoded telemetry dict
        self.last_rx_wall = None    # time.time() of last good packet
        self.packets_ok = 0
        self.packets_bad = 0
        self.packets_lost = 0       # inferred from sequence gaps
        self.rssi_dbm = None        # last appended RSSI byte, if enabled
        self.events = deque(maxlen=200)   # (time_str, text)
        self.track = deque(maxlen=20000)  # (lat, lon, gps_alt_m, time.time())
        self.max_alt = 0.0
        self._last_seq = None

    def record(self, decoded, rssi_dbm=None):
        with self._lock:
            now = time.time()
            self.packets_ok += 1
            if self._last_seq is not None:
                gap = (decoded["seq"] - self._last_seq) % 65536
                if 0 < gap - 1 < 100:  # wraps and replays tolerated
                    self.packets_lost += gap - 1
            self._last_seq = decoded["seq"]

            if rssi_dbm is not None:
                self.rssi_dbm = rssi_dbm

            text = None
            if decoded["type"] == packet.TYPE_STATUS:
                text = decoded["text"]
            else:
                self.latest = decoded
                self.last_rx_wall = now
                alt = decoded["baro_alt_rel_m"]
                if alt > self.max_alt:
                    self.max_alt = alt
                if decoded.get("fix_quality", 0) > 0:
                    self.track.append((decoded["lat"], decoded["lon"],
                                       decoded["gps_alt_m"], now))

            if decoded["state_name"] != getattr(self, "_last_state_name", None):
                text = text or f"state -> {decoded['state_name']}"
                self._last_state_name = decoded["state_name"]
            if text:
                self.events.append((time.strftime("%H:%M:%S"), text))

    def record_bad(self):
        with self._lock:
            self.packets_bad += 1

    def snapshot(self):
        with self._lock:
            rate_window = 0.0
            if self.latest:
                rate_window = time.time() - self.last_rx_wall
            return {
                "latest": self.latest,
                "rx_age_s": rate_window,
                "packets_ok": self.packets_ok,
                "packets_bad": self.packets_bad,
                "packets_lost": self.packets_lost,
                "rssi_dbm": self.rssi_dbm,
                "events": list(self.events),
                "track": list(self.track),
                "max_alt": self.max_alt,
            }


class Receiver(threading.Thread):
    """Background thread reading frames from a serial port or hex-line file."""

    def __init__(self, store: TelemetryStore, port=None, baud=9600,
                 rssi=False, replay_file=None, replay_rate_hz=2.0):
        super().__init__(daemon=True)
        self.store = store
        self.port = port
        self.baud = baud
        self.rssi = rssi
        self.replay_file = replay_file
        self.replay_rate_hz = replay_rate_hz
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    # -- frame synchronisation -------------------------------------------
    def _feed(self, buf: bytearray, chunk: bytes):
        """Append bytes; emit complete good frames. Resyncs on FL magic."""
        buf.extend(chunk)
        frame_len = packet.PACKET_SIZE + (1 if self.rssi else 0)
        i = 0
        while i + frame_len <= len(buf):
            if buf[i:i + 2] != packet.MAGIC:
                i += 1
                continue
            candidate = bytes(buf[i:i + packet.PACKET_SIZE])
            rssi_dbm = None
            if self.rssi:
                raw = buf[i + packet.PACKET_SIZE]
                rssi_dbm = -(256 - raw) if raw >= 128 else raw  # signed byte
            decoded = packet.decode(candidate)
            if decoded is None:
                self.store.record_bad()
                i += 1
                continue
            self.store.record(decoded, rssi_dbm)
            i += frame_len
        del buf[:i]
        # Keep the buffer from growing without bound on noise.
        if len(buf) > 4096:
            del buf[:-frame_len]

    # -- sources -----------------------------------------------------------
    def _run_serial(self):
        if serial is None:
            raise RuntimeError("pyserial not installed; pip install -r requirements.txt")
        ser = serial.Serial(self.port, self.baud, timeout=0.2)
        buf = bytearray()
        try:
            while not self._stop.is_set():
                chunk = ser.read(256)
                if chunk:
                    self._feed(buf, chunk)
        finally:
            ser.close()

    def _run_replay(self):
        """Replay frames from a hex-per-line file (written by
        tools/fake_transmitter.py --hex)."""
        delay = 1.0 / max(self.replay_rate_hz, 0.01)
        with open(self.replay_file) as f:
            while not self._stop.is_set():
                line = f.readline()
                if not line:
                    break
                line = line.strip()
                if not line:
                    continue
                try:
                    frame = bytes.fromhex(line)
                except ValueError:
                    continue
                buf = bytearray()
                self._feed(buf, frame)
                time.sleep(delay)

    def run(self):
        try:
            if self.replay_file:
                self._run_replay()
            else:
                self._run_serial()
        except Exception as exc:  # surfaced in the UI as an event
            self.store.record({
                "type": packet.TYPE_STATUS, "seq": 0, "state": 0,
                "state_name": "BOOT", "t_ms": 0,
                "text": f"receiver error: {exc}",
            })


class CsvLogger:
    """Writes every telemetry packet to gs_logs/<ts>/telemetry.csv."""

    FIELDS = [
        "rx_time", "seq", "state_name", "t_ms", "lat", "lon", "gps_alt_m",
        "sats", "fix_quality", "speed_kmh", "baro_alt_rel_m", "temperature_c",
        "humidity_pct", "ax_g", "ay_g", "az_g", "gx_dps", "gy_dps", "gz_dps",
        "geiger_cpm", "geiger_window_counts", "rssi_dbm",
    ]

    def __init__(self, log_dir):
        os.makedirs(log_dir, exist_ok=True)
        self.path = os.path.join(log_dir, "telemetry.csv")
        self._fh = open(self.path, "a", newline="")
        self._writer = csv.DictWriter(self._fh, fieldnames=self.FIELDS)
        if os.path.getsize(self.path) == 0:
            self._writer.writeheader()
        self._lock = threading.Lock()

    def log(self, store_snapshot):
        latest = store_snapshot["latest"]
        if latest is None:
            return
        with self._lock:
            row = {k: latest.get(k) for k in self.FIELDS}
            row["rx_time"] = time.strftime("%Y-%m-%dT%H:%M:%S",
                                           time.localtime()) \
                + f".{int((time.time() % 1) * 1000):03d}"
            row["rssi_dbm"] = store_snapshot["rssi_dbm"]
            self._writer.writerow(row)
            self._fh.flush()

    def close(self):
        self._fh.close()
