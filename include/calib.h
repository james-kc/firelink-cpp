#ifndef CALIB_H
#define CALIB_H

#include <vector>

// Robust one-shot estimate of the pad pressure for barometric altitude
// calibration.
//
// Sample corruption shows up as outliers (interleaved I2C reads, power
// dips), so a plain mean is wrong: a single spike skews `pad_pressure` and
// makes `relative_altitude` jump by hundreds of metres. This drops samples
// farther than `reject_delta_hpa` from the median and averages the inliers.
//
// Derived (and corrected) from the old quickPadPressure() in main.cpp.
float robustPadPressure(const std::vector<float> &samples,
                        float reject_delta_hpa,
                        float sanity_min_hpa,
                        float sanity_max_hpa,
                        float fallback_hpa);

#endif // CALIB_H