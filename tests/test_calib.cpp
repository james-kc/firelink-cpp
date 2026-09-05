#include "tests.h"

#include "calib.h"

#include <cstdio>
#include <cmath>

static int g_failures = 0;

#define CHECK(name, cond)                                    \
    do {                                                     \
        bool ok_ = (cond);                                   \
        std::printf("%s %s\n", ok_ ? "PASS" : "FAIL", name); \
        if (!ok_) ++g_failures;                              \
    } while (0)

static bool near(float a, float b, float eps = 0.01f) {
    return std::fabs(a - b) < eps;
}

int testCalib() {
    g_failures = 0;

    // Clean sample set -> plain mean, close to all samples.
    std::vector<float> clean = {1002.5f, 1002.4f, 1002.6f, 1002.3f, 1002.7f};
    float r = robustPadPressure(clean, 0.5f, 300.0f, 1200.0f, 1013.25f);
    CHECK("clean mean", near(r, 1002.5f));

    // One corrupt spike must not move the estimate (old bug: it skews by
    // ~2 hPa -> hundreds of metres of altitude error).
    std::vector<float> spiky = {1002.5f, 1002.4f, 1002.6f, 1006.9f, 1002.3f};
    r = robustPadPressure(spiky, 0.5f, 300.0f, 1200.0f, 1013.25f);
    CHECK("spike rejected", near(r, 1002.45f, 0.05f));

    // Out-of-sanity-range samples are dropped entirely.
    std::vector<float> dirty = {1002.5f, 0.0f, -5.0f, 1002.6f, 2500.0f};
    r = robustPadPressure(dirty, 0.5f, 300.0f, 1200.0f, 1013.25f);
    CHECK("sanity filter", near(r, 1002.55f, 0.05f));

    // All samples garbage -> fallback pad pressure.
    r = robustPadPressure({0.0f, -1.0f}, 0.5f, 300.0f, 1200.0f, 1013.25f);
    CHECK("fallback on garbage", near(r, 1013.25f));

    // Negative reject delta -> no rejection (plain mean) for debugging.
    r = robustPadPressure(spiky, -1.0f, 300.0f, 1200.0f, 1013.25f);
    CHECK("no-reject mean", near(r, (1002.5f + 1002.4f + 1002.6f + 1006.9f + 1002.3f) / 5.0f));

    // Everything outside the reject band -> fallback rather than a skewed mean.
    r = robustPadPressure({1000.0f, 1005.0f}, 0.1f, 300.0f, 1200.0f, 1013.25f);
    CHECK("all-outlier fallback", near(r, 1013.25f));

    return g_failures;
}