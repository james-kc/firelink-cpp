#ifndef TESTS_H
#define TESTS_H

// Test suites return the number of failed checks.
// If golden_path is non-null, testPacket also writes the golden telemetry
// frame (hex) there; groundstation/tests/test_packet.py compares against it.
int testPacket(const char *golden_path);
int testNmea();
int testConfig();

#endif
