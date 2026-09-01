#!/usr/bin/env python3
"""End-to-end ground station test: fake transmitter -> (pty | replay file)
-> receiver -> web API assertions.

Run from groundstation/:
    python tests/test_end_to_end.py
"""

import json
import os
import re
import subprocess
import sys
import time
import urllib.request

GS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PY = sys.executable

sys.path.insert(0, GS_DIR)
from firelink_gs import packet  # noqa: E402

WEB_PORT = 8199


def wait_for_web(url, timeout):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=2) as r:
                return json.load(r)
        except Exception:
            time.sleep(0.3)
    return None


def exercise_gs(args, duration):
    """Run the GS for `duration` seconds, polling the web API; return last
    live payload and whether ARMED/LANDED states were seen."""
    gs = subprocess.Popen(
        [PY, "-m", "firelink_gs"] + args + ["--no-tui", "--web",
                                            "--web-port", str(WEB_PORT)],
        cwd=GS_DIR, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    armed = landed = False
    got = None
    try:
        deadline = time.time() + duration
        while time.time() < deadline:
            j = wait_for_web(f"http://127.0.0.1:{WEB_PORT}/api/live", 3)
            if j is None:
                continue
            if j.get("latest"):
                got = j
                state = j["latest"]["state_name"]
                armed |= state == "ARMED"
                landed |= state == "LANDED"
            time.sleep(0.3)
    finally:
        gs.terminate()
        try:
            gs.wait(timeout=5)
        except subprocess.TimeoutExpired:
            gs.kill()
    return got, armed, landed


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    return 0 if cond else 1


def main():
    failures = 0

    hex_path = "/tmp/firelink_test_flight.hex"
    subprocess.run([PY, "tools/fake_transmitter.py", "--hex", hex_path,
                    "--arm-s", "2", "--flight-s", "6", "--beacon-s", "4",
                    "--rate", "10"],
                   cwd=GS_DIR, check=True)

    # --- Path 1: replay a hex file ---------------------------------------
    got, armed, landed = exercise_gs(
        ["--replay", hex_path, "--replay-rate", "50"], duration=15)
    failures += check("replay: telemetry received", got is not None)
    if got:
        failures += check("replay: states progressed", armed and landed)
        failures += check("replay: no crc failures",
                          got["packets_bad"] == 0)
        failures += check("replay: decent packet count",
                          got["packets_ok"] > 80)
        failures += check("replay: track built", len(got["track"]) > 5)
        failures += check("replay: max alt plausible", got["max_alt"] > 2500)

    # --- Path 2: live pty stream -----------------------------------------
    tx = subprocess.Popen(
        [PY, "-u", "tools/fake_transmitter.py",
         "--arm-s", "2", "--flight-s", "6", "--beacon-s", "4", "--rate", "8"],
        cwd=GS_DIR, stdout=subprocess.PIPE, text=True)
    try:
        line = tx.stdout.readline()
        match = re.search(r"(/dev/\S+)", line)
        failures += check("pty: transmitter started", match is not None)
        if match:
            got, armed, landed = exercise_gs(
                ["--port", match.group(1)], duration=14)
            failures += check("pty: telemetry received", got is not None)
            if got:
                failures += check("pty: states progressed", armed and landed)
                failures += check("pty: no crc failures",
                                  got["packets_bad"] == 0)
    finally:
        tx.terminate()

    print(f"\n{'ALL PASS' if failures == 0 else f'{failures} FAILURES'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
