"""Live terminal dashboard (rich)."""

import time

from rich.console import Console
from rich.layout import Layout
from rich.live import Live
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

STATE_STYLE = {
    "BOOT": "bold white on grey50",
    "PREFLIGHT": "bold white on blue",
    "ARMED": "bold white on red",
    "LANDED": "bold white on green",
}


def _kv(title, rows):
    t = Table.grid(padding=(0, 1))
    t.add_column(style="grey70", justify="right")
    t.add_column(style="bold white")
    for k, v in rows:
        t.add_row(k, v)
    return Panel(t, title=title, border_style="grey35")


def _render(store):
    snap = store.snapshot()
    lat = snap["latest"] or {}

    state = lat.get("state_name", "-")
    style = STATE_STYLE.get(state, "bold white")
    header = Text.assemble(
        (" FIRELINK GROUND STATION  ", "bold white"),
        (f" {state} ", style),
        (f"   uptime {lat.get('t_ms', 0) / 1000:9.1f} s", "grey70"),
    )

    fix = "yes" if lat.get("fix_quality", 0) else "NO"
    position = _kv("Position", [
        ("latitude", f"{lat.get('lat', 0):+.6f}"),
        ("longitude", f"{lat.get('lon', 0):+.6f}"),
        ("gps alt", f"{lat.get('gps_alt_m', 0)} m"),
        ("fix / sats", f"{fix} / {lat.get('sats', 0)}"),
        ("speed", f"{lat.get('speed_kmh', 0):.1f} km/h"),
    ])

    flight = _kv("Flight", [
        ("baro alt", f"{lat.get('baro_alt_rel_m', 0)} m"),
        ("max alt", f"{snap['max_alt']:.0f} m"),
        ("accel", f"({lat.get('ax_g', 0):+.2f}, {lat.get('ay_g', 0):+.2f}, "
                  f"{lat.get('az_g', 0):+.2f}) g"),
        ("gyro", f"({lat.get('gx_dps', 0):+.1f}, {lat.get('gy_dps', 0):+.1f}, "
                 f"{lat.get('gz_dps', 0):+.1f}) dps"),
    ])

    env = _kv("Environment", [
        ("temp", f"{lat.get('temperature_c', 0):.1f} C"),
        ("humidity", f"{lat.get('humidity_pct', 0)} %"),
        ("geiger", f"{lat.get('geiger_cpm', 0)} CPM"),
    ])

    rx_age = snap["rx_age_s"] if snap["latest"] else None
    age_txt = f"{rx_age:.1f} s" if rx_age is not None else "-"
    link_rows = [
        ("packets ok", str(snap["packets_ok"])),
        ("bad crc", str(snap["packets_bad"])),
        ("lost (seq)", str(snap["packets_lost"])),
        ("last rx", age_txt),
    ]
    if snap["rssi_dbm"] is not None:
        link_rows.append(("rssi", f"{snap['rssi_dbm']} dBm"))
    link = _kv("Link", link_rows)

    events = Table.grid(padding=(0, 1))
    events.add_column(style="grey50")
    events.add_column(style="white")
    for ts, text in snap["events"][-8:]:
        events.add_row(ts, text)
    events_panel = Panel(events or "(none yet)", title="Events",
                         border_style="grey35")

    layout = Layout()
    layout.split_column(
        Layout(header, size=1, name="header"),
        Layout(name="body"),
        Layout(events_panel, name="events", size=10),
    )
    layout["body"].split_row(
        Layout(position, name="pos"),
        Layout(flight, name="flight"),
        Layout(env, name="env"),
        Layout(link, name="link"),
    )
    return layout


def run_dashboard(store, logger=None, hz=2.0):
    """Blocking rich Live loop. Ctrl+C to exit."""
    console = Console()
    last_logged_seq = None
    with Live(_render(store), console=console, refresh_per_second=hz,
              screen=True) as live:
        try:
            while True:
                live.update(_render(store))
                if logger is not None:
                    snap = store.snapshot()
                    latest = snap["latest"]
                    if latest and latest["seq"] != last_logged_seq:
                        logger.log(snap)
                        last_logged_seq = latest["seq"]
                time.sleep(1.0 / hz)
        except KeyboardInterrupt:
            pass
