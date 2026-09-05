#include "calib.h"

#include <algorithm>
#include <cmath>

float robustPadPressure(const std::vector<float> &samples,
                        float reject_delta_hpa,
                        float sanity_min_hpa,
                        float sanity_max_hpa,
                        float fallback_hpa) {
    std::vector<float> sane;
    sane.reserve(samples.size());
    for (float s : samples) {
        if (s >= sanity_min_hpa && s <= sanity_max_hpa) sane.push_back(s);
    }
    if (sane.empty()) return fallback_hpa;

    std::vector<float> sorted = sane;
    std::sort(sorted.begin(), sorted.end());
    float median = sorted[sorted.size() / 2];

    if (reject_delta_hpa < 0.0f) {
        // Negative delta means "no rejection": plain mean (test/debug use).
        float sum = 0.0f;
        for (float s : sane) sum += s;
        return sum / (float)sane.size();
    }

    float sum = 0.0f;
    int inliers = 0;
    for (float s : sane) {
        if (std::fabs(s - median) <= reject_delta_hpa) {
            sum += s;
            ++inliers;
        }
    }
    return inliers ? sum / (float)inliers : fallback_hpa;
}