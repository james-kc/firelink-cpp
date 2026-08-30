#include "tests.h"

#include "config.h"

#include <cstdio>
#include <cstdlib>

static int g_failures = 0;

#define CHECK(name, cond)                                    \
    do {                                                     \
        bool ok_ = (cond);                                   \
        std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
        if (!ok_) ++g_failures;                              \
    } while (0)

int testConfig() {
    g_failures = 0;

    const char *path = "/tmp/firelink_test_config.conf";
    {
        std::remove(path);
        FILE *f = std::fopen(path, "w");
        std::fputs("# comment\n"
                   "pin.geiger=17\n"
                   "i2c.addr.gps=0x10\n"
                   "rate.telemetry_hz=2.5\n"
                   "arm.require_gps_fix=true\n"
                   "callsign=FIRELINK-1\n",
                   f);
        std::fclose(f);
    }

    Config cfg;
    CHECK("load", cfg.load(path));
    CHECK("int", cfg.getInt("pin.geiger", -1) == 17);
    CHECK("hex int", cfg.getInt("i2c.addr.gps", -1) == 0x10);
    CHECK("float", cfg.getFloat("rate.telemetry_hz", -1) > 2.49f);
    CHECK("bool", cfg.getBool("arm.require_gps_fix", false));
    CHECK("string", cfg.getString("callsign", "") == "FIRELINK-1");
    CHECK("missing int default", cfg.getInt("nope", 42) == 42);
    CHECK("missing bool default", !cfg.getBool("nope", false));

    cfg.set("pin.geiger", "23");
    CHECK("set+reload", cfg.getInt("pin.geiger", -1) == 23);

    std::string json = cfg.toJson();
    CHECK("json int raw", json.find("\"pin.geiger\":23") != std::string::npos);
    CHECK("json str quoted",
          json.find("\"callsign\":\"FIRELINK-1\"") != std::string::npos);

    CHECK("restart key", Config::isRestartRequiredKey("pin.geiger"));
    CHECK("restart key 2", Config::isRestartRequiredKey("lora.uart"));
    CHECK("live key", !Config::isRestartRequiredKey("rate.telemetry_hz"));

    CHECK("save", cfg.save(path));
    Config cfg2;
    CHECK("reload", cfg2.load(path));
    CHECK("round trip", cfg2.getInt("pin.geiger", -1) == 23);

    CHECK("missing file", !cfg.load("/tmp/definitely_not_here.conf"));

    return g_failures;
}
