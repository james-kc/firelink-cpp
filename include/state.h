#ifndef STATE_H
#define STATE_H

#include <cstdint>
#include <string>
#include <mutex>

// Flight state machine: BOOT is entered at process start, transitions to
// PREFLIGHT as soon as initialisation completes. ARMED is requested from the
// web page. LANDED is entered automatically (acceleration + altitude quiet
// for a configurable window) or manually via disarm after touchdown.
enum class FlightState : uint8_t {
    BOOT      = 0,
    PREFLIGHT = 1,
    ARMED     = 2,
    LANDED    = 3,
};

const char *flightStateName(FlightState s);

// A consistent point-in-time copy of everything the rest of the system
// (web server, telemetry thread, landing detector) needs to know about.
struct Snapshot {
    FlightState state = FlightState::BOOT;
    uint64_t boot_time_ms = 0;        // steady clock at process start
    uint64_t arm_time_ms = 0;         // steady clock when armed (0 = not armed yet)

    // GPS
    bool     gps_ok = false;          // sensor initialised
    bool     gps_fix = false;
    int      gps_fix_quality = 0;
    int      gps_sats = 0;
    double   gps_lat = 0.0, gps_lon = 0.0;
    float    gps_alt_m = 0.0f;
    float    gps_speed_kmh = 0.0f;
    uint64_t gps_updated_ms = 0;      // steady clock of last valid sentence

    // BME280
    bool     env_ok = false;
    float    pressure_hpa = 0.0f;
    float    baro_alt_rel_m = 0.0f;   // relative to pad pressure at arm time
    float    temperature_c = 0.0f;
    float    humidity_pct = 0.0f;
    uint64_t env_updated_ms = 0;

    // IMU
    bool     imu_ok = false;
    float    ax = 0, ay = 0, az = 0;  // m/s^2
    float    gx = 0, gy = 0, gz = 0;  // deg/s
    float    accel_mag = 0.0f;        // |a| m/s^2
    uint64_t imu_updated_ms = 0;

    // Geiger
    bool     geiger_ok = false;
    uint32_t geiger_cpm = 0;
    uint32_t geiger_window_counts = 0;
    uint64_t geiger_updated_ms = 0;

    // Derived / housekeeping
    float    max_alt_rel_m = 0.0f;
    float    max_accel_mag = 0.0f;
    float    pad_pressure_hpa = 0.0f;
    bool     recording = false;
    std::string session_dir;          // active data/<timestamp>/ directory
    uint16_t telemetry_seq = 0;
};

// Thread-safe holder for the shared Snapshot.
class SharedState {
public:
    // Setters used by sensor threads (fine-grained updates).
    void setState(FlightState s);
    void setGps(bool fix, int quality, int sats, double lat, double lon,
                float alt_m, float speed_kmh, uint64_t now_ms);
    void setGpsOk(bool ok);
    void setEnv(float pressure, float alt_rel, float temp, float hum, uint64_t now_ms);
    void setEnvOk(bool ok);
    void setImu(float ax, float ay, float az, float gx, float gy, float gz,
                float mag, uint64_t now_ms);
    void setImuOk(bool ok);
    void setGeiger(uint32_t cpm, uint32_t window_counts, uint64_t now_ms);
    void setGeigerOk(bool ok);
    void setPadPressure(float hpa);
    void setRecording(bool rec, const std::string &session_dir);
    void setTelemetrySeq(uint16_t seq);
    void noteMaxValues(float alt_rel, float accel_mag);
    void setArmTime(uint64_t t_ms);

    Snapshot get() const;

private:
    mutable std::mutex mu_;
    Snapshot snap_;
};

// Monotonic milliseconds since some arbitrary epoch (steady_clock based).
uint64_t steadyMs();

#endif // STATE_H
