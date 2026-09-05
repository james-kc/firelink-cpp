"""Firelink ground station entry point.

Examples:
    python -m firelink_gs --port /dev/tty.usbserial-XXXX --web
    python -m firelink_gs --replay flight.hex
    python tools/fake_transmitter.py     # then point --port at the pty shown
"""

import argparse
import os
import time

from .dashboard import run_dashboard
from .exporter import export_gpx, export_kml
from .receiver import CsvLogger, Receiver, TelemetryStore
from .web import run_web


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
    receiver.start()

    if args.web:
        log_root = os.path.dirname(log_dir.rstrip(os.sep)) or "gs_logs"
        run_web(store, port=args.web_port, log_root=log_root)
        print(f"Web map: http://127.0.0.1:{args.web_port}")
        print(f"Flights: http://127.0.0.1:{args.web_port}/flights")

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
                time.sleep(0.1)
        else:
            run_dashboard(store, logger)
    except KeyboardInterrupt:
        pass
    finally:
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
