#include "sensors/nmea.h"

#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>

static bool isNumeric(const std::string &s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return std::isdigit(c) || c == '.' || c == '-' || c == '+';
    });
}

static std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> tokens;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) tokens.push_back(tok);
    return tokens;
}

bool nmeaParseGga(const std::string &sentence, NmeaFix &data) {
    if (sentence.rfind("$GPGGA", 0) != 0 && sentence.rfind("$GNGGA", 0) != 0)
        return false;

    auto tokens = split(sentence);
    // Fields up to index 11 (geoid height) are used; a sentence stripped of
    // its checksum still parses.
    if (tokens.size() < 12) return false;

    data.hasFix = tokens[6] != "0";
    data.fixQuality = isNumeric(tokens[6]) ? std::stoi(tokens[6]) : 0;
    data.satellites = isNumeric(tokens[7]) ? std::stoi(tokens[7]) : 0;
    data.altitude_m = isNumeric(tokens[9]) ? std::stod(tokens[9]) : 0.0;
    data.hdop = isNumeric(tokens[8]) ? std::stod(tokens[8]) : 0.0;
    data.geoid_height_m = isNumeric(tokens[11]) ? std::stod(tokens[11]) : 0.0;

    // Latitude: ddmm.mmmmm[,N|S]
    if (isNumeric(tokens[2]) && tokens[2].size() > 2 && !tokens[3].empty()) {
        double lat = std::stod(tokens[2].substr(0, 2))
                   + std::stod(tokens[2].substr(2)) / 60.0;
        if (tokens[3] == "S") lat = -lat;
        data.latitude = lat;
    }

    // Longitude: dddmm.mmmmm[,E|W]
    if (isNumeric(tokens[4]) && tokens[4].size() > 3 && !tokens[5].empty()) {
        double lon = std::stod(tokens[4].substr(0, 3))
                   + std::stod(tokens[4].substr(3)) / 60.0;
        if (tokens[5] == "W") lon = -lon;
        data.longitude = lon;
    }

    data.utc_time = tokens[1];
    return true;
}

bool nmeaParseRmc(const std::string &sentence, NmeaFix &data) {
    if (sentence.rfind("$GPRMC", 0) != 0 && sentence.rfind("$GNRMC", 0) != 0)
        return false;

    auto tokens = split(sentence);
    if (tokens.size() < 12) return false;

    // Field 2: A = active fix, V = void.
    data.hasFix = (tokens[2] == "A");

    // Ground speed in knots -> km/h.
    if (isNumeric(tokens[7])) {
        data.speed_kmh = std::stod(tokens[7]) * 1.852;
    } else {
        data.speed_kmh = 0.0;
    }

    // Course over ground (deg) and date stamp ddmmyy.
    data.track_deg = isNumeric(tokens[8]) ? std::stod(tokens[8]) : 0.0;
    data.utc_date = tokens[9];

    data.utc_time = tokens[1];
    return true;
}
