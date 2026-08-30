#include "state.h"

#include <chrono>
#include <cmath>

const char *flightStateName(FlightState s) {
    switch (s) {
        case FlightState::BOOT:      return "BOOT";
        case FlightState::PREFLIGHT: return "PREFLIGHT";
        case FlightState::ARMED:     return "ARMED";
        case FlightState::LANDED:    return "LANDED";
    }
    return "UNKNOWN";
}

uint64_t steadyMs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void SharedState::setState(FlightState s) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.state = s;
}

void SharedState::setGps(bool fix, int quality, int sats, double lat, double lon,
                         float alt_m, float speed_kmh, uint64_t now_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.gps_fix = fix;
    snap_.gps_fix_quality = quality;
    snap_.gps_sats = sats;
    snap_.gps_lat = lat;
    snap_.gps_lon = lon;
    snap_.gps_alt_m = alt_m;
    snap_.gps_speed_kmh = speed_kmh;
    snap_.gps_updated_ms = now_ms;
}

void SharedState::setGpsOk(bool ok) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.gps_ok = ok;
}

void SharedState::setEnv(float pressure, float alt_rel, float temp, float hum, uint64_t now_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.pressure_hpa = pressure;
    snap_.baro_alt_rel_m = alt_rel;
    snap_.temperature_c = temp;
    snap_.humidity_pct = hum;
    snap_.env_updated_ms = now_ms;
}

void SharedState::setEnvOk(bool ok) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.env_ok = ok;
}

void SharedState::setImu(float ax, float ay, float az, float gx, float gy, float gz,
                         float mag, uint64_t now_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.ax = ax; snap_.ay = ay; snap_.az = az;
    snap_.gx = gx; snap_.gy = gy; snap_.gz = gz;
    snap_.accel_mag = mag;
    snap_.imu_updated_ms = now_ms;
    if (mag > snap_.max_accel_mag) snap_.max_accel_mag = mag;
}

void SharedState::setImuOk(bool ok) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.imu_ok = ok;
}

void SharedState::setGeiger(uint32_t cpm, uint32_t window_counts, uint64_t now_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.geiger_cpm = cpm;
    snap_.geiger_window_counts = window_counts;
    snap_.geiger_updated_ms = now_ms;
}

void SharedState::setGeigerOk(bool ok) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.geiger_ok = ok;
}

void SharedState::setPadPressure(float hpa) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.pad_pressure_hpa = hpa;
}

void SharedState::setRecording(bool rec, const std::string &session_dir) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.recording = rec;
    snap_.session_dir = session_dir;
}

void SharedState::setTelemetrySeq(uint16_t seq) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.telemetry_seq = seq;
}

void SharedState::noteMaxValues(float alt_rel, float accel_mag) {
    std::lock_guard<std::mutex> lk(mu_);
    if (alt_rel > snap_.max_alt_rel_m) snap_.max_alt_rel_m = alt_rel;
    if (accel_mag > snap_.max_accel_mag) snap_.max_accel_mag = accel_mag;
}

void SharedState::setArmTime(uint64_t t_ms) {
    std::lock_guard<std::mutex> lk(mu_);
    snap_.arm_time_ms = t_ms;
}

Snapshot SharedState::get() const {
    std::lock_guard<std::mutex> lk(mu_);
    return snap_;
}
