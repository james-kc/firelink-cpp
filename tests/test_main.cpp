#include "tests.h"

#include <cstdio>
#include <cstring>

// Host-side unit tests for the platform-independent parts of the flight
// computer (no Pi hardware required): `make test`.
//
// `run_tests --emit-golden PATH` additionally writes the golden telemetry
// frame consumed by groundstation/tests/test_packet.py.
int main(int argc, char **argv) {
    const char *golden_path = nullptr;
    if (argc > 2 && std::strcmp(argv[1], "--emit-golden") == 0)
        golden_path = argv[2];

    int failures = 0;
    failures += testPacket(golden_path);
    failures += testNmea();
    failures += testConfig();
    failures += testCalib();

    std::printf("\n%s\n", failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return failures == 0 ? 0 : 1;
}
