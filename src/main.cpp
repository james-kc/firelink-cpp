// Firelink flight computer.
//
// Boot flow:  load config -> start sensor threads -> start LoRa telemetry ->
//             start web server -> PREFLIGHT. The web page arms the computer
//             (4-digit confirmation code); while armed every sensor thread
//             streams to per-sensor CSVs in a timestamped session directory
//             and the telemetry thread keys the LoRa downlink. CSV rows are
//             decimated to a low pad rate until a launch is latched (baro
//             altitude gain or sustained high-g), then back-filled at full
//             rate from a RAM ring buffer so the launch is always captured in
//             full-rate data. After landing is detected (IMU + baro quiet)
//             the computer drops CSV rows to a low landed rate and the radio
//             to a low-rate GPS beacon until disarmed.

#include "config.h"
#include "state.h"
#include "sensors/gps.h"
#include "sensors/imu.h"
#include "sensors/bme280.h"
#include "sensors/geiger.h"
#include "telemetry/lora.h"
#include "telemetry/packet.h"
#include "outputs/buzzer_pwm.h"
#include "web/server.h"
#include "notes.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/statvfs.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------- util ----

static std::string utcTimestampMs() {
    // Wall-clock timestamp for CSV rows (Pi has no RTC; mirrors the format
    // used by firelink-py: dd/mm/YYYY HH:MM:SS.mmm).
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d.%03d",
                  tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)(ts.tv_nsec / 1000000));
    return buf;
}

static std::string sessionName() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

static long diskFreeMb(const std::string &path) {
    struct statvfs st;
    if (statvfs(path.c_str(), &st) != 0) return -1;
    return (long)((uint64_t)st.f_bavail * st.f_frsize / (1024 * 1024));
}

static std::string jsonEscape(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;
        }
    }
    return out;
}

// Pre-select recorder for one CSV stream. Three phases:
//   PAD:    rows (timestamp fixed at capture time) accumulate in a RAM ring
//           buffer; rows evicted from the buffer are written decimated to
//           rec.prelaunch_hz.
//   FLIGHT: the launch latch has been observed; the whole buffer is
//           back-filled at full rate once, then rows write through, so the
//           launch itself is always captured in full-rate data.
//   LANDED: rows write through decimated to rec.landed_hz.
// Rows are strictly time-ordered with no duplicates. Leaving PAD for any
// reason flushes the buffer, so no pre-launch rows are ever dropped.
class PreSelectRecorder {
public:
    enum class Mode { PAD, FLIGHT, LANDED };

    void configure(int stream_hz, float prelaunch_hz, float landed_hz,
                   int buffer_s) {
        capacity_ = std::max(1, stream_hz * std::max(1, buffer_s));
        pad_decim_ = decimation(stream_hz, prelaunch_hz);
        landed_decim_ = decimation(stream_hz, landed_hz);
        ring_.clear();
        pad_evict_ = 0;
        landed_seen_ = 0;
        latched_ = false;
    }

    // Records one row; returns the number of buffered rows back-filled at
    // the moment the pad phase is left, 0 otherwise.
    int push(const std::string &row, std::ofstream &out, Mode mode) {
        if (mode != Mode::PAD && !latched_) {
            latched_ = true;
            int backfilled = (int)ring_.size();
            while (!ring_.empty()) {
                out << ring_.front() << '\n';
                ring_.pop_front();
            }
            out << row << '\n';
            return backfilled;
        }
        if (mode == Mode::PAD) {
            ring_.push_back(row);
            if ((int)ring_.size() > capacity_) {
                if (pad_evict_ % pad_decim_ == 0) out << ring_.front() << '\n';
                ++pad_evict_;
                ring_.pop_front();
            }
        } else if (mode == Mode::FLIGHT) {
            out << row << '\n';
        } else { // LANDED: decimated write-through
            if (++landed_seen_ % landed_decim_ == 0) out << row << '\n';
        }
        return 0;
    }

private:
    static int decimation(int stream_hz, float hz) {
        return std::max(1, (int)std::lround(
            stream_hz / (double)std::max(0.01f, hz)));
    }

    int capacity_ = 1;
    int pad_decim_ = 1;
    int landed_decim_ = 1;
    long pad_evict_ = 0;
    long landed_seen_ = 0;
    bool latched_ = false;
    std::deque<std::string> ring_;
};

// ------------------------------------------------------------ flight app ----

class FlightComputer {
public:
    FlightComputer(Config &cfg, SharedState &state)
        : cfg_(cfg), state_(state),
          buzzer_((unsigned int)cfg.getInt("pin.buzzer", 4),
                  cfg.getString("gpio.chip", "gpiochip0")) {}

    // ------------------------------------------------------------ buzzer --
    void beepAsync(int ms) {
        std::thread([this, ms] {
            std::lock_guard<std::mutex> lk(buzzer_mu_);
            if (buzzer_ok_) buzzer_.beep(ms);
        }).detach();
    }

    void landedMelodyAsync() {
        std::thread([this] {
            std::lock_guard<std::mutex> lk(buzzer_mu_);
            if (!buzzer_ok_) return;
            std::vector<int> notes = {Notes::A5, Notes::Bb5, Notes::B5, Notes::C6};
            std::vector<int> dur = {100, 100, 100, 600};
            buzzer_.playMelody(notes, dur);
            // Recovery siren: rising/falling sweeps until disarm. The loop
            // re-checks the flight state each ~1.3 s sweep; disarm flips the
            // state to PREFLIGHT and the siren stops after the current sweep.
            while (state_.get().state == FlightState::LANDED) {
                for (int f = 600; f <= 1400; f += 50) buzzer_.tone(f, 20);
                for (int f = 1400; f >= 600; f -= 50) buzzer_.tone(f, 20);
            }
        }).detach();
    }

    // ----------------------------------------------------------- threads --

    void gpsThread(std::atomic<bool> &stop) {
        GPS gps(cfg_.getString("i2c.bus", "/dev/i2c-1"),
                (uint8_t)cfg_.getInt("i2c.addr.gps", 0x10));
        if (!gps.begin(cfg_.getInt("gps.update_hz", 5))) {
            state_.setGpsOk(false);
            return;
        }
        state_.setGpsOk(true);
        std::cout << "GPS thread running" << std::endl;

        while (!stop.load()) {
            gps.poll();
            const NmeaFix &f = gps.fix();
            state_.setGps(f.hasFix, f.fixQuality, f.satellites, f.latitude,
                          f.longitude, (float)f.altitude_m,
                          (float)f.speed_kmh, steadyMs());

            {
                Snapshot rs = state_.get();
                if (rs.recording) {
                    int backfilled = 0;
                    {
                        std::lock_guard<std::mutex> lk(csv_mu_);
                        if (csv_gps_.is_open() && f.hasFix) {
                            double lat_deg, lat_min, lon_deg, lon_min;
                            splitDegMin(f.latitude, lat_deg, lat_min);
                            splitDegMin(f.longitude, lon_deg, lon_min);
                            std::ostringstream row;
                            row << utcTimestampMs() << ','
                                << f.utc_time << ','
                                << f.fixQuality << ','
                                << f.latitude << ','
                                << f.longitude << ','
                                << (int)lat_deg << ','
                                << lat_min << ','
                                << (int)lon_deg << ','
                                << lon_min << ','
                                << f.satellites << ','
                                << f.altitude_m << ','
                                << f.speed_kmh / 1.852 << ','
                                << f.track_deg << ','
                                << f.hdop << ','
                                << f.geoid_height_m;
                            backfilled = rec_gps_.push(row.str(), csv_gps_,
                                                       recordMode(rs));
                            csv_gps_.flush();
                        }
                    }
                    if (backfilled > 0)
                        logEvent("LAUNCH backfill: " + std::to_string(backfilled) +
                                 " gps rows");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    }

    void imuThread(std::atomic<bool> &stop) {
        IMU imu(cfg_.getString("i2c.bus", "/dev/i2c-1"),
                (uint8_t)cfg_.getInt("i2c.addr.imu", 0x6A));
        IMU::Settings s;
        s.accel_range_g = cfg_.getInt("imu.accel_range_g", 16);
        s.gyro_range_dps = cfg_.getInt("imu.gyro_range_dps", 2000);
        s.odr_hz = cfg_.getInt("imu.odr_hz", 833);
        if (!imu.begin(s)) {
            state_.setImuOk(false);
            return;
        }
        state_.setImuOk(true);
        std::cout << "IMU thread running (ODR " << s.odr_hz << " Hz)" << std::endl;

        int period_us = 1000000 / std::max(1, cfg_.getInt("rate.imu_hz", 200));
        auto next = std::chrono::steady_clock::now();

        while (!stop.load()) {
            float ax, ay, az, gx, gy, gz;
            imu.readAccel(ax, ay, az);
            imu.readGyro(gx, gy, gz);
            float mag = std::sqrt(ax * ax + ay * ay + az * az);
            state_.setImu(ax, ay, az, gx, gy, gz, mag, steadyMs());

            {
                Snapshot rs = state_.get();
                if (rs.recording) {
                    int backfilled = 0;
                    {
                        std::lock_guard<std::mutex> lk(csv_mu_);
                        if (csv_imu_.is_open()) {
                            std::ostringstream row;
                            row << utcTimestampMs() << ','
                                << ax << ',' << ay << ',' << az << ','
                                << gx << ',' << gy << ',' << gz;
                            backfilled = rec_imu_.push(row.str(), csv_imu_,
                                                       recordMode(rs));
                            // Flush opportunistically ~once per second of samples.
                            if (++imu_rows_since_flush_ >= 100) {
                                csv_imu_.flush();
                                imu_rows_since_flush_ = 0;
                            }
                        }
                    }
                    if (backfilled > 0)
                        logEvent("LAUNCH backfill: " + std::to_string(backfilled) +
                                 " accelerometer rows");
                }
            }

            next += std::chrono::microseconds(period_us);
            std::this_thread::sleep_until(next);
        }
    }

    void envThread(std::atomic<bool> &stop) {
        BME280 bme(cfg_.getString("i2c.bus", "/dev/i2c-1"),
                   (uint8_t)cfg_.getInt("i2c.addr.bme280", 0x76));
        if (!bme.begin()) {
            state_.setEnvOk(false);
            return;
        }
        state_.setEnvOk(true);

        // Initial pad pressure (10 s). Recalibrate on the pad via the web UI.
        state_.setPadPressure(quickPadPressure(bme, 10));
        std::cout << "Environment thread running" << std::endl;

        int period_ms = 1000 / std::max(1, cfg_.getInt("rate.bme_hz", 10));
        while (!stop.load()) {
            float pressure = bme.readPressure();
            float temp = bme.readTemperature();
            float hum = bme.readHumidity();
            float pad = state_.get().pad_pressure_hpa;
            float alt_rel = 44330.0f * (1.0f - std::pow(pressure / pad, 0.1903f));
            state_.setEnv(pressure, alt_rel, temp, hum, steadyMs());
            state_.noteMaxValues(alt_rel, 0);

            {
                Snapshot rs = state_.get();
                if (rs.recording) {
                    int backfilled = 0;
                    {
                        std::lock_guard<std::mutex> lk(csv_mu_);
                        if (csv_env_.is_open()) {
                            std::ostringstream row;
                            row << utcTimestampMs() << ','
                                << alt_rel << ','
                                << pressure << ','
                                << temp << ','
                                << hum;
                            backfilled = rec_env_.push(row.str(), csv_env_,
                                                       recordMode(rs));
                            csv_env_.flush();
                        }
                    }
                    if (backfilled > 0)
                        logEvent("LAUNCH backfill: " + std::to_string(backfilled) +
                                 " barometer rows");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
        }
    }

    void geigerThread(std::atomic<bool> &stop) {
        Geiger geiger(cfg_.getString("gpio.chip", "gpiochip0"),
                      (unsigned int)cfg_.getInt("pin.geiger", 17));
        if (!geiger.begin()) {
            state_.setGeigerOk(false);
            return;
        }
        state_.setGeigerOk(true);
        std::cout << "Geiger thread running" << std::endl;

        const uint64_t window_ms =
            (uint64_t)std::max(1, cfg_.getInt("geiger.window_s", 60)) * 1000;
        std::deque<uint64_t> pulses;

        while (!stop.load()) {
            int n = geiger.waitForPulses(1000);
            uint64_t now = steadyMs();
            while (n-- > 0) pulses.push_back(now);
            while (!pulses.empty() && now - pulses.front() > window_ms)
                pulses.pop_front();

            // Scale windowed counts to counts-per-minute.
            uint32_t window_counts = (uint32_t)pulses.size();
            uint32_t cpm = (uint32_t)std::llround(
                (double)window_counts * 60000.0 / (double)window_ms);
            state_.setGeiger(cpm, window_counts, now);

            {
                Snapshot rs = state_.get();
                if (rs.recording) {
                    int backfilled = 0;
                    {
                        std::lock_guard<std::mutex> lk(csv_mu_);
                        if (csv_geiger_.is_open()) {
                            std::ostringstream row;
                            row << utcTimestampMs() << ','
                                << window_counts << ','
                                << cpm;
                            backfilled = rec_geiger_.push(row.str(), csv_geiger_,
                                                          recordMode(rs));
                            csv_geiger_.flush();
                        }
                    }
                    if (backfilled > 0)
                        logEvent("LAUNCH backfill: " + std::to_string(backfilled) +
                                 " geiger rows");
                }
            }
        }
    }

    void telemetryThread(std::atomic<bool> &stop) {
        LoRa::Settings ls;
        ls.uart = cfg_.getString("lora.uart", "/dev/serial0");
        ls.baud = cfg_.getInt("lora.baud", 9600);
        ls.m0_pin = cfg_.getInt("pin.lora_m0", 22);
        ls.m1_pin = cfg_.getInt("pin.lora_m1", 27);
        ls.aux_pin = cfg_.getInt("pin.lora_aux", -1);
        ls.chip = cfg_.getString("gpio.chip", "gpiochip0");
        ls.configure = cfg_.getBool("lora.configure", false);
        ls.address = (uint16_t)cfg_.getInt("lora.address", 0);
        ls.net_id = (uint8_t)cfg_.getInt("lora.net_id", 0);
        ls.air_baud = cfg_.getInt("lora.air_baud", 2400);
        ls.tx_power_dbm = cfg_.getInt("lora.tx_power_dbm", 22);
        ls.channel = (uint8_t)cfg_.getInt("lora.channel", 18);
        ls.rssi_append = cfg_.getBool("lora.rssi_append", false);
        ls.fixed_mode = cfg_.getBool("lora.fixed_mode", false);

        LoRa lora(ls);
        if (!lora.begin()) {
            std::cerr << "Telemetry: LoRa unavailable, link down" << std::endl;
            return;
        }

        std::cout << "Telemetry thread running" << std::endl;
        uint16_t seq = 0;

        while (!stop.load()) {
            Snapshot snap = state_.get();
            float hz = (snap.state == FlightState::LANDED)
                       ? cfg_.getFloat("rate.landed_telemetry_hz", 0.2f)
                       : cfg_.getFloat("rate.telemetry_hz", 2.0f);
            if (hz < 0.05f) hz = 0.05f;

            TelemetryFields f = telemetryFieldsFromSnapshot(snap);
            uint8_t buf[FIRELINK_PACKET_SIZE];
            packetEncodeTelemetry(seq, f, buf);
            lora.send(buf, sizeof(buf));
            state_.setTelemetrySeq(seq);
            seq++;

            // Any queued status text (state transitions) goes out next.
            std::string status;
            {
                std::lock_guard<std::mutex> lk(status_mu_);
                status.swap(pending_status_);
            }
            if (!status.empty()) {
                uint32_t t_ms = (snap.arm_time_ms > 0)
                                ? (uint32_t)(steadyMs() - snap.arm_time_ms) : 0;
                packetEncodeStatus(seq, (uint8_t)snap.state, t_ms, status, buf);
                lora.send(buf, sizeof(buf));
                seq++;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds((int)(1000.0f / hz)));
        }
    }

    // --------------------------------------------------------- arm/disarm --

    std::string arm(const std::string &code, bool force) {
        // The 4-digit code shown on the web page is always required,
        // including when forcing past failed pre-arm checks. A wrong code
        // rotates to a fresh one so it cannot be guessed against a fixed
        // value.
        {
            std::lock_guard<std::mutex> lk(code_mu_);
            if (code.empty() || code != arm_code_) {
                rotateArmCode();
                return "{\"ok\":false,\"error\":\"wrong arm code\"}";
            }
        }

        Snapshot snap = state_.get();
        if (snap.state == FlightState::ARMED)
            return "{\"ok\":false,\"error\":\"already armed\"}";

        std::vector<std::string> failed;
        if (!snap.gps_ok) failed.push_back("gps offline");
        if (!snap.imu_ok) failed.push_back("imu offline");
        if (!snap.env_ok) failed.push_back("bme280 offline");
        if (cfg_.getBool("arm.require_gps_fix", true) && !snap.gps_fix)
            failed.push_back("no gps fix");

        if (!failed.empty() && !force) {
            std::ostringstream os;
            os << "{\"ok\":false,\"error\":\"pre-arm checks failed\",\"checks_failed\":[";
            for (size_t i = 0; i < failed.size(); ++i) {
                if (i) os << ',';
                os << "\"" << failed[i] << "\"";
            }
            os << "]}";
            return os.str();
        }

        openSession();
        state_.setArmTime(steadyMs());
        state_.setState(FlightState::ARMED);
        launch_detected_.store(false);
        launch_streak_ = 0;
        land_history_.clear();
        logEvent("ARMED" + std::string(force ? " (forced)" : ""));
        sendStatus("ARMED");
        beepAsync(250);
        std::cout << "Flight computer ARMED" << (force ? " (forced)" : "") << std::endl;
        return "{\"ok\":true}";
    }

    std::string disarm() {
        Snapshot snap = state_.get();
        if (snap.state != FlightState::ARMED && snap.state != FlightState::LANDED)
            return "{\"ok\":false,\"error\":\"not armed\"}";

        closeSession();
        state_.setArmTime(0);
        state_.setState(FlightState::PREFLIGHT);
        launch_detected_.store(false);
        launch_streak_ = 0;
        land_history_.clear();
        sendStatus("DISARMED");
        beepAsync(80);
        std::cout << "Flight computer disarmed" << std::endl;
        return "{\"ok\":true}";
    }

    // Fresh 4-digit arming code for the web page; regenerated on every page
    // load and after every failed arm attempt.
    std::string armCodeJson() {
        std::lock_guard<std::mutex> lk(code_mu_);
        rotateArmCode();
        std::ostringstream os;
        os << "{\"code\":\"" << arm_code_ << "\"}";
        return os.str();
    }

    // Launch/landing detection, polled from the main loop while ARMED.
    // Landing is only considered once a launch has been latched (baro
    // altitude gain or sustained high-g), so a long armed wait on the pad can
    // never satisfy the landing criteria. The latch also flips the CSV
    // recorders from decimated pad-rate to full-rate recording.
    void checkLanding() {
        Snapshot snap = state_.get();
        if (snap.state != FlightState::ARMED) {
            land_history_.clear();
            launch_streak_ = 0;
            return;
        }

        if (!launch_detected_.load()) {
            float launch_alt = cfg_.getFloat("land.launch_alt_m", 20.0f);
            float launch_accel = cfg_.getFloat("land.launch_accel_ms2", 25.0f);
            int confirm_s = std::max(1, cfg_.getInt("land.launch_confirm_s", 2));

            bool tripped = (snap.baro_alt_rel_m > launch_alt) ||
                           (snap.accel_mag > launch_accel);
            launch_streak_ = tripped ? launch_streak_ + 1 : 0;
            if (launch_streak_ >= confirm_s) {
                launch_detected_.store(true);
                std::ostringstream ev;
                ev << "LAUNCH detected baro_alt=" << snap.baro_alt_rel_m
                   << "m accel=" << snap.accel_mag << "m/s2";
                logEvent(ev.str());
                sendStatus("LAUNCHED");
                std::cout << "Launch detected -> full-rate recording"
                          << std::endl;
            }
            return;
        }

        float min_flight_s = cfg_.getFloat("land.min_flight_s", 20);
        if (snap.arm_time_ms == 0 ||
            (steadyMs() - snap.arm_time_ms) < (uint64_t)(min_flight_s * 1000)) {
            return;
        }

        land_history_.push_back({steadyMs(), snap.accel_mag, snap.baro_alt_rel_m});

        // Count-based window: polling at ~1 Hz, keep the last window_s
        // samples. The previous age-based evict-then-check could never pass:
        // the poll period slightly exceeds 1 s, so the oldest sample crossed
        // the age limit and was evicted one tick before the check could see
        // a full window.
        size_t need = (size_t)std::max(1.0f, cfg_.getFloat("land.window_s", 15));
        while (land_history_.size() > need)
            land_history_.pop_front();
        if (land_history_.size() < need)
            return; // window not yet full

        float accel_tol = cfg_.getFloat("land.accel_tol", 1.5f);
        float alt_tol = cfg_.getFloat("land.alt_tol_m", 3.0f);

        float alt_min = land_history_.front().alt, alt_max = alt_min;
        bool quiet = true;
        for (const auto &s : land_history_) {
            if (std::fabs(s.accel_mag - 9.80665f) > accel_tol) { quiet = false; break; }
            alt_min = std::min(alt_min, s.alt);
            alt_max = std::max(alt_max, s.alt);
        }

        if (quiet && (alt_max - alt_min) < alt_tol) {
            state_.setState(FlightState::LANDED);
            logEvent("LANDED detected");
            sendStatus("LANDED");
            landedMelodyAsync();
            std::cout << "Landing detected -> low-rate beacon mode" << std::endl;
        } else if (cfg_.getBool("land.debug", false)) {
            float max_dev = 0.0f;
            for (const auto &s : land_history_)
                max_dev = std::max(max_dev, std::fabs(s.accel_mag - 9.80665f));
            std::cout << "landdbg n=" << land_history_.size()
                      << " quiet=" << quiet << " max_accel_dev=" << max_dev
                      << " alt_spread=" << (alt_max - alt_min)
                      << " tol=" << accel_tol << "/" << alt_tol << std::endl;
        }
    }

    // --------------------------------------------------------- web glue ----

    std::string statusJson() {
        Snapshot s = state_.get();
        std::ostringstream os;
        os << std::fixed;
        os.precision(6);
        os << "{\"state\":\"" << flightStateName(s.state) << "\","
           << "\"uptime_s\":" << (steadyMs() - boot_ms_) / 1000 << ","
           << "\"gps_ok\":" << (s.gps_ok ? "true" : "false") << ","
           << "\"gps_fix\":" << (s.gps_fix ? "true" : "false") << ","
           << "\"gps_sats\":" << s.gps_sats << ","
           << "\"gps_lat\":" << s.gps_lat << ","
           << "\"gps_lon\":" << s.gps_lon << ","
           << "\"gps_alt_m\":" << s.gps_alt_m << ","
           << "\"gps_speed_kmh\":" << s.gps_speed_kmh << ","
           << "\"env_ok\":" << (s.env_ok ? "true" : "false") << ","
           << "\"pressure_hpa\":" << s.pressure_hpa << ","
           << "\"baro_alt_rel_m\":" << s.baro_alt_rel_m << ","
           << "\"temperature_c\":" << s.temperature_c << ","
           << "\"humidity_pct\":" << s.humidity_pct << ","
           << "\"pad_pressure_hpa\":" << s.pad_pressure_hpa << ","
           << "\"launch_latched\":" << (launch_detected_.load() ? "true" : "false") << ","
           << "\"imu_ok\":" << (s.imu_ok ? "true" : "false") << ","
           << "\"accel_mag\":" << s.accel_mag << ","
           << "\"geiger_ok\":" << (s.geiger_ok ? "true" : "false") << ","
           << "\"geiger_cpm\":" << s.geiger_cpm << ","
           << "\"recording\":" << (s.recording ? "true" : "false") << ","
           << "\"session_dir\":\"" << jsonEscape(s.session_dir) << "\","
           << "\"disk_free_mb\":" << diskFreeMb(cfg_.getString("data_dir", "data")) << ","
           << "\"telemetry_seq\":" << s.telemetry_seq << ","
           << "\"max_alt_rel_m\":" << s.max_alt_rel_m << ","
           << "\"max_accel_mag\":" << s.max_accel_mag << "}";
        return os.str();
    }

    std::string readDataFile(const std::string &rel) {
        std::string base = cfg_.getString("data_dir", "data");
        std::ifstream f(fs::path(base) / rel, std::ios::binary);
        if (!f.is_open()) return "";
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string listDataJson() {
        std::string base = cfg_.getString("data_dir", "data");
        std::ostringstream os;
        os << "{\"files\":[";
        bool first = true;
        std::error_code ec;
        for (auto &sess : fs::directory_iterator(base, ec)) {
            if (!sess.is_directory()) continue;
            for (auto &f : fs::directory_iterator(sess.path(), ec)) {
                if (!f.is_regular_file()) continue;
                if (!first) os << ',';
                first = false;
                os << "{\"path\":\"" << jsonEscape(
                          sess.path().filename().string() + "/" +
                          f.path().filename().string())
                   << "\",\"size\":" << (uint64_t)f.file_size(ec) << "}";
            }
        }
        os << "]}";
        return os.str();
    }

    std::string recalibrate() {
        // Runs in the HTTP handler thread; takes a few seconds (fine on the
        // pad, and the response carries the fresh pad pressure).
        BME280 bme(cfg_.getString("i2c.bus", "/dev/i2c-1"),
                   (uint8_t)cfg_.getInt("i2c.addr.bme280", 0x76));
        if (!bme.begin())
            return "{\"ok\":false,\"error\":\"bme280 unavailable\"}";
        float pad = quickPadPressure(bme, 10);
        state_.setPadPressure(pad);
        std::ostringstream os;
        os << "{\"ok\":true,\"pad_pressure_hpa\":" << pad << "}";
        return os.str();
    }

    int run() {
        boot_ms_ = steadyMs();
        state_.setState(FlightState::BOOT);

        std::string data_dir = cfg_.getString("data_dir", "data");
        std::error_code ec;
        fs::create_directories(data_dir, ec);

        buzzer_ok_ = buzzer_.begin();
        beepAsync(60);

        std::atomic<bool> stop{false};

        // Install a bare SIGINT handler so Ctrl+C unwinds cleanly.
        static std::atomic<bool> *stop_ptr = &stop;
        std::signal(SIGINT, [](int) { stop_ptr->store(true); });

        std::vector<std::thread> threads;
        threads.emplace_back([this, &stop] { gpsThread(stop); });
        threads.emplace_back([this, &stop] { imuThread(stop); });
        threads.emplace_back([this, &stop] { envThread(stop); });
        threads.emplace_back([this, &stop] { geigerThread(stop); });
        threads.emplace_back([this, &stop] { telemetryThread(stop); });

        // Web API.
        WebServer::Handlers h;
        h.getStatusJson = [this] { return statusJson(); };
        h.arm = [this](const std::string &code, bool force) { return arm(code, force); };
        h.getArmCode = [this] { return armCodeJson(); };
        h.disarm = [this] { return disarm(); };
        h.recalibrate = [this] { return recalibrate(); };
        h.readDataFile = [this](const std::string &rel) { return readDataFile(rel); };
        h.listDataJson = [this] { return listDataJson(); };

        WebServer web(cfg_.getInt("web.port", 80), cfg_, h, data_dir);
        if (!web.start()) {
            std::cerr << "Web server failed to start (need root for port 80?)"
                      << std::endl;
        }

        state_.setState(FlightState::PREFLIGHT);
        beepAsync(60);
        std::cout << "Firelink ready: PREFLIGHT" << std::endl;

        while (!stop.load()) {
            checkLanding();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        web.stop();
        stop.store(true);
        closeSession();
        for (auto &t : threads) {
            if (t.joinable()) t.join();
        }
        return 0;
    }

private:
    Config &cfg_;
    SharedState &state_;
    BuzzerPWM buzzer_;
    std::mutex buzzer_mu_;
    bool buzzer_ok_ = false;
    uint64_t boot_ms_ = 0;

    // CSV session files (guarded by csv_mu_).
    std::mutex csv_mu_;
    std::ofstream csv_gps_, csv_imu_, csv_env_, csv_geiger_, events_;
    std::string session_path_;
    int imu_rows_since_flush_ = 0;

    // Status text queued for the LoRa downlink.
    std::mutex status_mu_;
    std::string pending_status_;

    struct LandSample { uint64_t t_ms; float accel_mag; float alt; };
    std::deque<LandSample> land_history_;

    // Launch latch: written by the main loop in checkLanding(), also read by
    // the sensor threads to flip the pre-select recorders to full rate.
    std::atomic<bool> launch_detected_{false};
    int launch_streak_ = 0;   // main loop only

    // Pre-select CSV recorders (decimated pad rate + full-rate launch
    // backfill). Configured per session in openSession(), used only under
    // csv_mu_.
    PreSelectRecorder rec_gps_, rec_imu_, rec_env_, rec_geiger_;

    // Arming code: the web page must echo back the displayed 4-digit code.
    std::mutex code_mu_;
    std::string arm_code_;
    std::mt19937 rng_{std::random_device{}()};

    // Caller holds code_mu_.
    void rotateArmCode() {
        std::uniform_int_distribution<int> dist(0, 9999);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%04d", dist(rng_));
        arm_code_ = buf;
    }

    static void splitDegMin(double decimal, double &deg, double &min) {
        double a = std::fabs(decimal);
        deg = (int)a;
        min = (a - deg) * 60.0;
    }

    // Recorder phase for one fresh CSV row: decimated pad rate until the
    // launch latch trips, full rate while armed after that, decimated again
    // once landed. LANDED is checked first: the launch latch may still be
    // latched then, but the flight is over and we want the landed rate.
    PreSelectRecorder::Mode recordMode(const Snapshot &s) {
        if (s.state == FlightState::LANDED) return PreSelectRecorder::Mode::LANDED;
        return launch_detected_.load() ? PreSelectRecorder::Mode::FLIGHT
                                       : PreSelectRecorder::Mode::PAD;
    }

    float quickPadPressure(BME280 &bme, int samples) {
        float sum = 0.0f;
        int got = 0;
        for (int i = 0; i < samples; ++i) {
            float p = bme.readPressure();
            if (p > 300.0f && p < 1200.0f) { sum += p; ++got; }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return got ? sum / got : 1013.25f;
    }

    void openSession() {
        std::lock_guard<std::mutex> lk(csv_mu_);
        std::string base = cfg_.getString("data_dir", "data");
        session_path_ = base + "/" + sessionName();
        std::error_code ec;
        fs::create_directories(session_path_, ec);

        // The pre-trigger buffer must cover the launch detector's own
        // latency (confirmation polls plus the 1 s main-loop tick) so no
        // launch data is lost even when launch latches slowly.
        int confirm_s = std::max(1, cfg_.getInt("land.launch_confirm_s", 2));
        int buffer_s = std::max(1, cfg_.getInt("rec.launch_buffer_s", 15));
        int min_buffer_s = confirm_s + 5;
        if (buffer_s < min_buffer_s) {
            std::cout << "Recording: rec.launch_buffer_s=" << buffer_s
                      << " too small for land.launch_confirm_s=" << confirm_s
                      << ", using " << min_buffer_s << " s" << std::endl;
            buffer_s = min_buffer_s;
        }
        float prelaunch_hz =
            std::max(0.01f, cfg_.getFloat("rec.prelaunch_hz", 1.0f));
        float landed_hz =
            std::max(0.01f, cfg_.getFloat("rec.landed_hz", 1.0f));

        rec_gps_.configure(25, prelaunch_hz, landed_hz, buffer_s);
        rec_imu_.configure(std::max(1, cfg_.getInt("rate.imu_hz", 200)),
                           prelaunch_hz, landed_hz, buffer_s);
        rec_env_.configure(std::max(1, cfg_.getInt("rate.bme_hz", 10)),
                           prelaunch_hz, landed_hz, buffer_s);
        rec_geiger_.configure(1, prelaunch_hz, landed_hz, buffer_s);

        csv_gps_.open(session_path_ + "/gps.csv");
        csv_gps_ << "thread_datetime,datetime,fix,latitude,longitude,"
                    "latitude_degrees,latitude_minutes,longitude_degrees,"
                    "longitude_minutes,satellites,altitude_m,speed_knots,"
                    "track_angle_deg,horizontal_dilution,height_geoid\n";

        csv_imu_.open(session_path_ + "/accelerometer.csv");
        csv_imu_ << "timestamp,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z\n";

        csv_env_.open(session_path_ + "/barometer.csv");
        csv_env_ << "timestamp,relative_altitude,pressure_hpa,temperature_c,"
                    "humidity_pct\n";

        csv_geiger_.open(session_path_ + "/geiger.csv");
        csv_geiger_ << "timestamp,window_counts,cpm\n";

        events_.open(session_path_ + "/events.log", std::ios::app);
        state_.setRecording(true, session_path_);
    }

    void closeSession() {
        std::lock_guard<std::mutex> lk(csv_mu_);
        if (events_.is_open()) {
            events_ << utcTimestampMs() << " session closed\n";
        }
        csv_gps_.close(); csv_imu_.close(); csv_env_.close();
        csv_geiger_.close(); events_.close();
        state_.setRecording(false, "");
    }

    void logEvent(const std::string &msg) {
        std::lock_guard<std::mutex> lk(csv_mu_);
        if (events_.is_open()) {
            events_ << utcTimestampMs() << " " << msg << "\n";
            events_.flush();
        }
    }

    void sendStatus(const std::string &text) {
        // Queued for the telemetry thread, which emits a type-2 status frame
        // on the next telemetry slot.
        std::lock_guard<std::mutex> lk(status_mu_);
        pending_status_ = text;
    }
};

// ------------------------------------------------------------------ main ----

int main(int argc, char **argv) {
    std::string config_path = (argc > 1) ? argv[1] : "firelink.conf";

    Config cfg;
    cfg.setFilePath(config_path);
    if (!cfg.load(config_path)) {
        std::cout << "No config file at " << config_path
                  << " - running on compiled-in defaults" << std::endl;
    }

    SharedState state;
    FlightComputer app(cfg, state);
    return app.run();
}
