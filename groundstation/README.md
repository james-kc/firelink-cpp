# Firelink ground station

Receives the LoRa telemetry downlink from the flight computer on a second
Waveshare SX1262 LoRa HAT plugged into the laptop over USB (the HAT's
onboard CP2102 makes it appear as a serial port). Shows a live terminal
dashboard, an optional web map, logs every packet, and exports GPX/KML
tracks so you can walk to the rocket.

## Install

```bash
cd groundstation
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
```

## Run

```bash
# Terminal dashboard (find the port with: ls /dev/tty.usbserial*  /  /dev/ttyUSB*)
python -m firelink_gs --port /dev/tty.usbserial-XXXX

# Add the web map at http://127.0.0.1:8080
python -m firelink_gs --port /dev/tty.usbserial-XXXX --web

# Useful flags
--baud 9600        # module UART baud
--rssi             # modules append an RSSI byte (needs lora.rssi_append=true flight-side)
--no-tui           # headless: plain text lines only
--log-dir DIR      # where telemetry.csv / track.gpx / track.kml go
```

On exit, `track.gpx` and `track.kml` are written into the session log
directory — import them into Google Earth/maps for recovery navigation.

## Test without any hardware

```bash
# Option A: live synthetic flight over a virtual serial port
python tools/fake_transmitter.py
# prints e.g. /dev/ttys012 — then in another terminal:
python -m firelink_gs --port /dev/ttys012 --web

# Option B: replay a hex file
python tools/fake_transmitter.py --hex flight.hex
python -m firelink_gs --replay flight.hex

# Codec unit tests (includes the C++ <-> Python golden frame check)
python tests/test_packet.py
```

## Meshtastic

Your portable Meshtastic node flies in the rocket as an independent
position beacon — it needs nothing from this software. Track it with the
normal Meshtastic app on your phone/laptop alongside this ground station.
The two position sources are deliberately independent for redundancy.
