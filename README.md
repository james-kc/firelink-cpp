# firelink

Flight computer and ground station for the Firelink avionics flown on the
Scarecrow and Banshee rockets (MACH 24/25).

- **Flight computer** (C++, Raspberry Pi Zero 2 W): pre-flight WiFi
  configuration page, web arming, high-rate sensor logging and LoRa
  telemetry. Lives at the repo root (`src/`, `include/`).
- **Ground station** (Python, macOS/Linux laptop): receives the LoRa
  downlink from a second HAT over USB, shows a live terminal dashboard +
  web map, logs everything, exports GPX/KML. Lives in `groundstation/`.
- The radio contract between them is documented in
  [`docs/telemetry_protocol.md`](docs/telemetry_protocol.md).

## Hardware

| Device | Bus | Address / pin (defaults, all configurable) |
|---|---|---|
| GTop PA1010D GPS | I2C | `/dev/i2c-1` @ `0x10` |
| LSM6DS3/33 IMU | I2C | `0x6A` |
| BME280 env sensor | I2C | `0x76` |
| Gravity Geiger counter (SEN0463) | GPIO pulse | BCM 17 |
| Buzzer | GPIO | BCM 4 |
| Waveshare SX1262 868 MHz LoRa HAT | UART + GPIO | `/dev/serial0`, M0/M1 = BCM 22/27 |

## How it works

1. **Power on** → `firelink.service` starts the flight binary, the Pi's WiFi
   access point (`firelink`) comes up, sensors self-test, GPS begins its
   warm-up.
2. **Pre-flight** → connect a phone/laptop to the Pi's WiFi and open
   <http://10.42.0.1>. The page shows live sensor status and lets you edit
   settings (sampling rates, radio channel, GPIO pins, ...), recalibrate pad
   pressure, and **arm** the rocket.
3. **Armed** → arming requires typing the 4-digit code freshly generated on
   the web page each load (anti-accidental-arm interlock). Once armed, all
   sensors stream to timestamped CSVs under `data/<session>/` (columns match
   firelink-py so existing `post_flight/` analysis keeps working) and GPS +
   flight telemetry is transmitted over LoRa. While waiting on the pad the
   CSVs are written at a decimated pad rate (`rec.prelaunch_hz`); when a
   launch is detected, the last `rec.launch_buffer_s` seconds are back-filled
   at full rate from a RAM ring buffer and recording continues at full rate,
   so the launch is never lost and long pad holds don't bloat the files.
   Every row is timestamped, so the pad-rate → full-rate boundary is visible
   in the row spacing (and `events.log` records the moment explicitly).
4. **Landed** → detected automatically (launch must first be latched, then
   IMU and baro quiet for `land.window_s` seconds); the buzzer plays, and
   the radio drops to a low-rate **GPS beacon** for recovery. Walk up,
   rejoin the WiFi, disarm, and download the CSVs straight from the page.
5. **Meshtastic** → the portable Meshtastic node flown in the rocket is a
   fully independent position beacon (own power + RF); watch it in the
   usual Meshtastic app.

## Pi setup (once)

```bash
sudo apt update && sudo apt install -y libgpiod-dev gpiod i2c-tools
sudo raspi-config   # Interface Options: enable I2C; enable serial port,
                    # DISABLE serial login shell
bash system/setup_ap.sh                       # creates WiFi AP "firelink"
sudo cp system/firelink.service /etc/systemd/system/
sudo systemctl enable --now firelink.service  # start at boot
```

Copy `firelink.conf.example` to `firelink.conf` to tweak persisted settings
(or just use the web page once it's running).

> Note: the buzzer/Geiger/LoRa GPIO code uses the libgpiod **v1** API.
> Raspberry Pi OS Bullseye ships v1; on Bookworm install the v1
> compatibility package or build libgpiod 1.6 from source.

## Build & run

```bash
make            # builds ./firelink
make test       # host-side unit tests (no Pi hardware needed)
sudo ./firelink [firelink.conf]
```

or with CMake:

```bash
cmake -B build && cmake --build build
```

## Ground station

See [`groundstation/README.md`](groundstation/README.md). Quick version:

```bash
cd groundstation
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt
python -m firelink_gs --port /dev/tty.usbserial-XXXX --web
```

You can try it without any hardware: `python tools/fake_transmitter.py`
creates a virtual serial port broadcasting a synthetic flight — point
`--port` at it.

## Repository layout

```
src/, include/        flight computer (C++)
  sensors/            gps, imu, bme280, geiger, nmea parser
  telemetry/          LoRa UART driver, 48-byte packet codec
  web/                embedded HTTP server + control page
  outputs/            buzzer (GPIO + PWM melodies)
groundstation/        Python ground station + test tools
system/               systemd unit + WiFi AP setup
tests/                host-runnable C++ tests (packet, nmea, config)
docs/                 telemetry protocol spec
```
