// ElektroDump — MSU-GS calibration LUT + unit conversion (spec §1.4).
#pragma once

#include <string>
#include <vector>

#include "image/image.h"

namespace ed::image {

enum class CalUnit { Reflectance, BrightnessTemperature, Radiance, Counts };

struct CalibrationTable {
    bool valid = false;
    // lut[channel][raw] -> calibrated value; raw in 0..1023
    std::vector<std::vector<double>> lut;
    std::vector<double> wavelengths_um;  // per channel
    std::vector<CalUnit> units;          // per channel
    std::vector<double> wavenumber_cm1;  // derived: 1e4 / wavelength_um
};

// Load resources/calibration/msugs_table.json; missing file => valid=false
// (calibration becomes a pass-through, logged once).
CalibrationTable load_calibration(const std::string& path, std::string* error);

// Apply the LUT; invalid pixels (raw == 0 || raw > 1023) are cleared in
// out_valid. An invalid table passes raw counts through unchanged.
Image16 calibrate(const Image16& raw, const Mask& valid,
                  const CalibrationTable& table, int channel_id,
                  Mask* out_valid);

}  // namespace ed::image
