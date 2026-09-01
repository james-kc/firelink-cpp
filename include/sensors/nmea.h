#ifndef NMEA_H
#define NMEA_H

#include <string>

// Pure NMEA-0183 sentence parsers, separated from any I/O so they can be
// unit tested on a development machine.
//
// Both functions fill fields into `data` (which the caller seeds from the
// last known-good fix, so fields not present in a sentence are preserved)
// and return true if the sentence type matched and parsed.

struct NmeaFix {
    bool   hasFix = false;
    int    fixQuality = 0;
    int    satellites = 0;
    double latitude = 0.0;    // decimal degrees, +N
    double longitude = 0.0;   // decimal degrees, +E
    double altitude_m = 0.0;
    double speed_kmh = 0.0;
    double hdop = 0.0;          // horizontal dilution (GGA)
    double geoid_height_m = 0;  // geoid separation (GGA)
    double track_deg = 0.0;     // course over ground (RMC)
    std::string utc_time;       // hhmmss.ss field, as received
    std::string utc_date;       // ddmmyy field from RMC, as received
};

bool nmeaParseGga(const std::string &sentence, NmeaFix &data);
bool nmeaParseRmc(const std::string &sentence, NmeaFix &data);

#endif // NMEA_H
