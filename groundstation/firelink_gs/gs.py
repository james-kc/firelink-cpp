"""Firelink ground station entry point.

Examples:
    python -m firelink_gs --port /dev/tty.usbserial-XXXX --web
    python -m firelink_gs --replay flight.hex
    python tools/fake_transmitter.py     # then point --port at the pty shown

The first time you use --rssi against a real radio the ground module must be
configured once (M0/M1 pin flip) for it to append an RSSI byte to received
frames; the setup prompts walk you through it. Afterwards a marker file
(~/.firelink_gs_dongle.json) means normal startups run with no prompting.
"""

import argparse
import json
import os
import sys
import threading
import time

from . import packet
from .dashboard import run_dashboard
from .exporter import export_gpx, export_kml
from .receiver import CsvLogger, Receiver, TelemetryStore
from .web import run_web

try:
    import serial  # pyserial
except ImportError:  # replay-only operation
    serial = None

# E22/SE22-style register bits.
RSSI_APPEND_BIT = 0x40  # REG3 bit 6
CFG_MARKER_FILE = os.path.expanduser("~/.firelink_gs_dongle.json")


def build_parser():
    p = argparse.ArgumentParser(
        prog="firelink-gs",
        description="Firelink ground station (SX1262 LoRa HAT over USB/serial)")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--port", help="serial port, e.g. /dev/tty.usbserial-*")
    src.add_argument("--replay", help="replay a hex-frame log instead of serial")
    p.add_argument("--baud", type=int, default=9600)
    p.add_argument("--rssi", action="store_true",
                   help="modules append an RSSI byte after each packet")
    p.add_argument("--skip-rssi-config", action="store_true",
                   help="don't run the interactive module setup during startup")
    p.add_argument("--no-tx", action="store_true",
                   help="don't transmit link-report beacons to the payload")
    p.add_argument("--beacon-rate", type=float, default=5.0,
                   help="seconds between link-report beacons (default 5)")
    p.add_argument("--replay-rate", type=float, default=2.0,
                   help="replay speed, frames per second")
    p.add_argument("--web", action="store_true", help="also serve a web map")
    p.add_argument("--web-port", type=int, default=8080)
    p.add_argument("--no-tui", action="store_true",
                   help="don't start the terminal dashboard")
    p.add_argument("--no-log", action="store_true",
                   help="don't write a session telemetry.csv")
    p.add_argument("--log-dir", default=None,
                   help="default gs_logs/<timestamp>/")
    p.add_argument("--export", action="store_true", default=True,
                   help="write track.gpx/track.kml on exit (default on)")
    return p


# ---------------------------------------------------------------------------
# One-time ground-module RSSI-append setup
# ---------------------------------------------------------------------------

def _probe_config(ser, tries=5):
    """Ask the module (config mode) for its 9 register bytes. None on
    failure. Uses the E22-style read command C1 00 09."""
    for _ in range(tries):
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        ser.write(bytes([0xC1, 0x00, 0x09]))
        ser.flush()
        data = ser.read(32)
        if len(data) >= 12 and data[0] == 0xC1 and data[1] == 0x00 and data[2] == 0x09:
            return list(data[3:12])
        time.sleep(0.3)
    return None


def _write_config(ser, regs):
    """Write all registers (E22-style C2 00 09 + 9 bytes). True on ack."""
    cmd = bytes([0xC2, 0x00, 0x09]) + bytes(regs)
    ser.reset_input_buffer()
    ser.write(cmd)
    ser.flush()
    data = ser.read(32)
    return (len(data) >= 3 and data[0] == 0xC1
            and data[1] == 0x00 and data[2] == 0x09)


def _load_marker(port):
    try:
        with open(CFG_MARKER_FILE) as f:
            m = json.load(f)
        return m if m.get("port") == port else None
    except Exception:
        return None


def _save_marker(port, reg3):
    try:
        with open(CFG_MARKER_FILE, "w") as f:
            json.dump({"port": port, "reg3": reg3,
                       "saved_at": time.strftime("%Y-%m-%dT%H:%M:%S")}, f)
    except OSError as e:
        print(f"warning: couldn't save dongle marker: {e}")


def _prompt(text):
    """Interactive confirmation; False if stdin isn't interactive."""
    if sys.stdin.isatty():
        print(text)
        input("Press ENTER when done...")
        return True
    print(text)
    print("(stdin is not interactive; continuing anyway)")
    return False


def maybe_setup_rssi(port, baud, skip=False):
    """Ensure the ground module appends an RSSI byte to received frames.

    Returns True if RSSI should be expected (module configured), False if the
    user chose to run without it. No-op when `skip` is set (assume the module
    is already configured, or accept n/a)."""
    if skip:
        print("--skip-rssi-config: skipping module setup; RSSI may show n/a.")
        return True
    if serial is None:
        print("pyserial missing; cannot configure the module.")
        return False

    # Already in config mode? (user may have left the pins there)
    ser = serial.Serial(port, baud, timeout=0.3)
    try:
        cfg = _probe_config(ser)
    finally:
        ser.close()

    if cfg is not None:
        print("Ground module is in config mode (M0=0, M1=1).")
    elif _load_marker(port):
        print("Ground module previously configured for RSSI-append; "
              "assuming it persisted across power cycles.")
        return True
    else:
        print("RSSI needs a one-time setup on the ground module:")
        for attempt in (1, 2, 3):
            if not _prompt(f"  [{attempt}] Set M0=0, M1=1 (config mode), "
                           f"then press ENTER.\n"
                           f"      (Module stays plugged in; only the "
                           f"DIP/jumper pins move.)"):
                return False
            ser = serial.Serial(port, baud, timeout=0.3)
            try:
                cfg = _probe_config(ser)
            finally:
                ser.close()
            if cfg is not None:
                break
            print("  No config response yet - are M0/M1 correct?")
        if cfg is None:
            print("Couldn't enter config mode after 3 tries.")
            print("Run without --rssi, or check the module's M0/M1 wiring.")
            return False

    # cfg holds 9 bytes: ADDH ADDL NETID REG0 REG1 REG2 REG3 CRYPT_H CRYPT_L.
    regs = list(cfg)
    reg3_expected = regs[6] | RSSI_APPEND_BIT
    if regs[6] == reg3_expected:
        print("RSSI-append already active.")
    else:
        regs[6] = reg3_expected
        ser = serial.Serial(port, baud, timeout=0.3)
        try:
            ok = _write_config(ser, regs)
        finally:
            ser.close()
        if not ok:
            print("Module didn't acknowledge the config write.")
            return False
        print("RSSI-append configured (REG3 = 0x%02X)." % reg3_expected)
    _save_marker(port, reg3_expected)

    # Leave config mode: normal operation needs transparent mode.
    if cfg is not None:
        _prompt("Set M0=0, M1=0 (transparent mode, both to GND), "
                "then press ENTER to start.")
    return True


class BeaconSender(threading.Thread):
    """Periodically transmits a type-3 link report to the payload."""

    def __init__(self, receiver, store, interval):
        super().__init__(daemon=True)
        self._receiver = receiver
        self._store = store
        self._interval = max(interval, 0.5)
        self._stop = threading.Event()
        self._seq = 0

    def stop(self):
        self._stop.set()

    def run(self):
        while not self._stop.wait(self._interval):
            self._send()

    def _send(self):
        snap = self._store.snapshot()
        latest = snap["latest"] or {}
        rssi = snap["rssi_dbm"]
        flags = 1 if rssi is not None else 0
        rx_age_ms = (int(snap["rx_age_s"] * 1000)
                     if latest else 0xFFFFFFFF)
        self._seq = (self._seq + 1) & 0xFFFF
        frame = packet.encode_link_report(self._seq, {
            "last_rx_seq": latest.get("seq", 0) if latest else 0,
            "packets_ok": snap["packets_ok"],
            "packets_lost": snap["packets_lost"],
            "packets_bad": snap["packets_bad"],
            "rssi_dbm": rssi if rssi is not None else 0,
            "rate_cps": snap["rate_cps"],
            "rx_age_ms": rx_age_ms,
            "flags": flags,
        })
        self._receiver.write(frame)


def main(argv=None):
    args = build_parser().parse_args(argv)

    log_dir = args.log_dir or os.path.join(
        "gs_logs", time.strftime("%Y%m%dT%H%M%S"))

    store = TelemetryStore()
    logger = None if args.no_log else CsvLogger(log_dir)
    if logger:
        print(f"Logging received telemetry to {logger.path}")

    receiver = Receiver(store, port=args.port, baud=args.baud,
                        rssi=args.rssi, replay_file=args.replay,
                        replay_rate_hz=args.replay_rate)

    if args.port and args.rssi:
        maybe_setup_rssi(args.port, args.baud, skip=args.skip_rssi_config)

    receiver.start()

    beacons = None
    if args.port and not args.replay and not args.no_tx:
        beacons = BeaconSender(receiver, store, args.beacon_rate)
        beacons.start()
        if logger:
            store.events.append((
                time.strftime("%H:%M:%S"),
                f"link beacons every {args.beacon_rate:.0f}s"))

    if args.web:
        log_root = os.path.dirname(log_dir.rstrip(os.sep)) or "gs_logs"
        run_web(store, port=args.web_port, log_root=log_root)
        print(f"Web map: http://127.0.0.1:{args.web_port}")
        print(f"Flights: http://127.0.0.1:{args.web_port}/flights")

    rssi_hint_printed = False
    try:
        if args.no_tui:
            # Headless mode: log quietly until Ctrl+C.
            last_seq = None
            while True:
                snap = store.snapshot()
                latest = snap["latest"]
                if latest and latest["seq"] != last_seq:
                    if logger:
                        logger.log(snap)
                    last_seq = latest["seq"]
                    print(f"[{latest['state_name']:9s}] "
                          f"lat={latest['lat']:+.6f} lon={latest['lon']:+.6f} "
                          f"alt={latest['gps_alt_m']} m  sats={latest['sats']}")
                if (args.rssi and snap["packets_ok"] > 30
                        and not snap["rssi_confirmed"]
                        and not rssi_hint_printed):
                    print("RSSI requested but no RSSI has appeared - is the "
                          "ground module configured to append it? Re-run "
                          "(without --skip-rssi-config) to fix the pins.")
                    rssi_hint_printed = True
                time.sleep(0.1)
        else:
            run_dashboard(store, logger)
    except KeyboardInterrupt:
        pass
    finally:
        if beacons:
            beacons.stop()
        receiver.stop()
        store.events.append((time.strftime("%H:%M:%S"), "session ended"))
        if args.export and store.snapshot()["track"]:
            gpx = export_gpx(os.path.join(log_dir or ".", "track.gpx"),
                             store.snapshot()["track"])
            kml = export_kml(os.path.join(log_dir or ".", "track.kml"),
                             store.snapshot()["track"])
            print(f"Exported {gpx} and {kml}")
        if logger:
            logger.close()


if __name__ == "__main__":
    main()