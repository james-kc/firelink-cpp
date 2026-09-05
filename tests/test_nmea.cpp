#include "tests.h"

#include "sensors/nmea.h"

#include <cstdio>
#include <cmath>

static int g_failures = 0;

#define CHECK(name, cond)                                    \
    do {                                                     \
        bool ok_ = (cond);                                   \
        std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
        if (!ok_) ++g_failures;                              \
    } while (0)

static bool near(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) < eps;
}

int testNmea() {
    g_failures = 0;

    // Realistic GGA: fix, 9 sats, Edinburgh-ish position.
    NmeaFix fix;
    bool ok = nmeaParseGga(
        "$GNGGA,123519.00,5550.5807,N,00314.4027,W,1,09,0.9,545.4,M,47.0,M,,",
        fix);
    CHECK("gga parsed", ok);
    CHECK("gga fix", fix.hasFix && fix.fixQuality == 1);
    CHECK("gga sats", fix.satellites == 9);
    CHECK("gga lat", near(fix.latitude, 55.0 + 50.5807 / 60.0, 1e-5));
    CHECK("gga lon sign", fix.longitude < 0);
    CHECK("gga lon", near(fix.longitude, -(3.0 + 14.4027 / 60.0), 1e-5));
    CHECK("gga alt", near(fix.altitude_m, 545.4));
    CHECK("gga hdop", near(fix.hdop, 0.9));
    CHECK("gga geoid", near(fix.geoid_height_m, 47.0));

    // RMC: speed conversion and void status.
    ok = nmeaParseRmc(
        "$GNRMC,123520.00,A,5550.5808,N,00314.4026,W,15.2,84.4,300825,,,A",
        fix);
    CHECK("rmc parsed", ok);
    CHECK("rmc fix", fix.hasFix);
    CHECK("rmc speed", near(fix.speed_kmh, 15.2 * 1.852, 1e-3));
    CHECK("rmc track", near(fix.track_deg, 84.4, 1e-3));
    CHECK("rmc date", fix.utc_date == "300825");
    CHECK("rmc keeps gga lat", near(fix.latitude, 55.0 + 50.5807 / 60.0, 1e-5));

    // Void RMC drops the fix.
    ok = nmeaParseRmc("$GNRMC,123521.00,V,,,,,,,300825,,,N", fix);
    CHECK("void rmc parsed", ok);
    CHECK("void rmc no fix", !fix.hasFix);

    // GGA fix quality 0 must drop an already-cached fix (the case the web
    // UI depends on when the module reports "no fix" while still alive).
    NmeaFix fix0;
    fix0.hasFix = true;
    fix0.satellites = 9;
    ok = nmeaParseGga(
        "$GNGGA,123522.00,5550.5809,N,00314.4025,W,0,00,0.0,0.0,M,47.0,M,,",
        fix0);
    CHECK("gga 0 parsed", ok);
    CHECK("gga 0 no fix", !fix0.hasFix);
    CHECK("gga 0 sats", fix0.satellites == 0);

    // Non-matching sentences are rejected cleanly.
    CHECK("rejects gsa", !nmeaParseGga("$GPGSA,A,3,,,,,,,,,,,,,,,,", fix));
    CHECK("rejects short", !nmeaParseGga("$GNGGA,1,2,3", fix));

    return g_failures;
}
