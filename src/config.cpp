#include "config.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

static std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

bool Config::isRestartRequiredKey(const std::string &key) {
    static const std::vector<std::string> prefixes = {
        "pin.", "gpio.", "i2c.", "imu.", "lora.uart", "lora.baud",
        "lora.configure", "lora.channel", "lora.address", "lora.net_id",
        "lora.air_baud", "lora.tx_power", "lora.rssi_append", "lora.fixed_mode",
        "gps.update_hz", "geiger.", "data_dir",
    };
    for (const auto &p : prefixes) {
        if (key.rfind(p, 0) == 0) return true;
    }
    return false;
}

bool Config::load(const std::string &path) {
    std::lock_guard<std::mutex> lk(mu_);
    path_ = path;

    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(s.substr(0, eq));
        std::string v = trim(s.substr(eq + 1));
        if (!k.empty()) values_[k] = v;
    }
    return true;
}

bool Config::save(const std::string &path) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::string p = path.empty() ? path_ : path;
    if (p.empty()) return false;

    std::ofstream f(p, std::ios::trunc);
    if (!f.is_open()) return false;

    f << "# Firelink flight computer configuration.\n"
         "# Editable at runtime through the pre-flight web page.\n";
    for (const auto &kv : values_) {
        f << kv.first << "=" << kv.second << "\n";
    }
    return true;
}

std::string Config::getString(const std::string &key, const std::string &def) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = values_.find(key);
    return it == values_.end() ? def : it->second;
}

int Config::getInt(const std::string &key, int def) const {
    std::string v = getString(key, "");
    if (v.empty()) return def;
    try {
        // Honour 0x-prefixed hex so device addresses read naturally.
        if (v.rfind("0x", 0) == 0 || v.rfind("0X", 0) == 0)
            return std::stoi(v, nullptr, 16);
        return std::stoi(v);
    } catch (...) { return def; }
}

float Config::getFloat(const std::string &key, float def) const {
    std::string v = getString(key, "");
    if (v.empty()) return def;
    try { return std::stof(v); } catch (...) { return def; }
}

bool Config::getBool(const std::string &key, bool def) const {
    std::string v = getString(key, "");
    if (v.empty()) return def;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return def;
}

bool Config::set(const std::string &key, const std::string &value) {
    std::lock_guard<std::mutex> lk(mu_);
    bool existed = values_.count(key) > 0;
    values_[key] = value;
    return existed;
}

std::map<std::string, std::string> Config::all() const {
    std::lock_guard<std::mutex> lk(mu_);
    return values_;
}

static bool isInteger(const std::string &s) {
    static const std::regex re("^-?[0-9]+$");
    return std::regex_match(s, re);
}

static bool isFloat(const std::string &s) {
    static const std::regex re("^-?[0-9]*\\.[0-9]+([eE][-+]?[0-9]+)?$");
    return std::regex_match(s, re);
}

static std::string jsonEscape(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

std::string Config::toJson() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::ostringstream os;
    os << "{";
    bool first = true;
    for (const auto &kv : values_) {
        if (!first) os << ",";
        first = false;
        os << "\"" << jsonEscape(kv.first) << "\":";
        const std::string &v = kv.second;
        if (isInteger(v) || isFloat(v) || v == "true" || v == "false") {
            os << v;
        } else {
            os << "\"" << jsonEscape(v) << "\"";
        }
    }
    os << "}";
    return os.str();
}
