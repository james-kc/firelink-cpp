#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <map>
#include <mutex>
#include <vector>

// Simple key=value configuration store.
//
// Loaded once at boot from a plain-text file (see firelink.conf.example),
// edited at runtime through the web UI and persisted back to disk.
// All values are stored as strings; typed getters apply sensible defaults
// when a key is missing or malformed.
class Config {
public:
    // Keys that only take effect after a process restart (pin numbers,
    // device paths, sensor ranges are all applied at initialisation time).
    static bool isRestartRequiredKey(const std::string &key);

    bool load(const std::string &path);
    bool save(const std::string &path) const;

    std::string getString(const std::string &key, const std::string &def = "") const;
    int         getInt(const std::string &key, int def = 0) const;
    float       getFloat(const std::string &key, float def = 0.0f) const;
    bool        getBool(const std::string &key, bool def = false) const;

    // Returns true if the key existed already.
    bool set(const std::string &key, const std::string &value);

    std::map<std::string, std::string> all() const;

    // Serialise the whole config as a JSON object. Numeric and boolean
    // looking values are emitted raw, everything else is quoted.
    std::string toJson() const;

    // Path of the file we were last loaded from / saved to.
    const std::string &filePath() const { return path_; }
    void setFilePath(const std::string &p) { path_ = p; }

private:
    mutable std::mutex mu_;
    std::map<std::string, std::string> values_;
    std::string path_;
};

#endif // CONFIG_H
