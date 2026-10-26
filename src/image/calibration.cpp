#include "image/calibration.h"

#include <algorithm>
#include <cstddef>

#include "util/fs_util.h"
#include "util/json_util.h"

namespace ed::image {

namespace {

CalUnit parse_unit(const std::string& text) {
    if (text == "reflectance") {
        return CalUnit::Reflectance;
    }
    if (text == "brightness_temperature") {
        return CalUnit::BrightnessTemperature;
    }
    if (text == "radiance") {
        return CalUnit::Radiance;
    }
    return CalUnit::Counts;
}

}  // namespace

CalibrationTable load_calibration(const std::string& path, std::string* error) {
    CalibrationTable table;
    if (!fs::exists(path)) {
        if (error != nullptr) {
            *error = "calibration table not found: " + path;
        }
        return table;
    }
    bool ok = false;
    const std::string text = fs::read_file(path, &ok);
    if (!ok) {
        if (error != nullptr) {
            *error = "cannot read calibration table: " + path;
        }
        return table;
    }
    std::string parse_error;
    const json::Value root = json::Value::parse(text, &parse_error);
    if (!parse_error.empty()) {
        if (error != nullptr) {
            *error = "failed to parse " + path + ": " + parse_error;
        }
        return table;
    }
    const json::Value* channels = root.find("channels");
    if (channels == nullptr || !channels->is_array()) {
        if (error != nullptr) {
            *error = path + ": 'channels' must be an array";
        }
        return table;
    }

    int max_id = -1;
    for (std::size_t i = 0; i < channels->size(); ++i) {
        const json::Value& entry = (*channels)[i];
        const json::Value* id = entry.find("id");
        if (id != nullptr && id->is_number()) {
            max_id = std::max(max_id, static_cast<int>(id->as_int()));
        }
    }
    if (max_id < 0) {
        if (error != nullptr) {
            *error = path + ": no channels with an 'id'";
        }
        return table;
    }

    const std::size_t count = static_cast<std::size_t>(max_id) + 1;
    table.lut.assign(count, {});
    table.wavelengths_um.assign(count, 0.0);
    table.units.assign(count, CalUnit::Counts);
    table.wavenumber_cm1.assign(count, 0.0);

    for (std::size_t i = 0; i < channels->size(); ++i) {
        const json::Value& entry = (*channels)[i];
        const json::Value* id = entry.find("id");
        if (id == nullptr || !id->is_number()) {
            continue;
        }
        const int channel = static_cast<int>(id->as_int());
        if (channel < 0 || channel > max_id) {
            continue;
        }
        const json::Value* wavelength = entry.find("wavelength_um");
        if (wavelength != nullptr && wavelength->is_number()) {
            table.wavelengths_um[static_cast<std::size_t>(channel)] =
                wavelength->as_double();
            if (wavelength->as_double() > 0.0) {
                table.wavenumber_cm1[static_cast<std::size_t>(channel)] =
                    1.0e4 / wavelength->as_double();
            }
        }
        const json::Value* unit = entry.find("unit");
        if (unit != nullptr && unit->is_string()) {
            table.units[static_cast<std::size_t>(channel)] =
                parse_unit(unit->as_string());
        }
        const json::Value* lut = entry.find("lut");
        if (lut != nullptr && lut->is_array()) {
            std::vector<double>& values =
                table.lut[static_cast<std::size_t>(channel)];
            values.reserve(lut->size());
            for (std::size_t k = 0; k < lut->size(); ++k) {
                values.push_back((*lut)[k].as_double());
            }
        }
    }

    for (const std::vector<double>& values : table.lut) {
        if (!values.empty()) {
            table.valid = true;
            break;
        }
    }
    if (!table.valid && error != nullptr) {
        *error = path + ": no channel LUT data";
    }
    return table;
}

Image16 calibrate(const Image16& raw, const Mask& valid,
                  const CalibrationTable& table, int channel_id,
                  Mask* out_valid) {
    Image16 out = raw;
    Mask out_mask;
    out_mask.resize(raw.width, raw.height);

    const bool have_lut =
        table.valid && channel_id >= 0 &&
        channel_id < static_cast<int>(table.lut.size()) &&
        !table.lut[static_cast<std::size_t>(channel_id)].empty();

    if (!have_lut) {
        // Pass-through: keep raw counts and the incoming validity mask.
        if (valid.data.size() == out_mask.data.size()) {
            out_mask.data = valid.data;
        } else {
            std::fill(out_mask.data.begin(), out_mask.data.end(), 1);
        }
        if (out_valid != nullptr) {
            *out_valid = std::move(out_mask);
        }
        return out;
    }

    const CalUnit unit =
        channel_id < static_cast<int>(table.units.size())
            ? table.units[static_cast<std::size_t>(channel_id)]
            : CalUnit::Counts;
    const std::vector<double>& lut =
        table.lut[static_cast<std::size_t>(channel_id)];

    for (std::size_t i = 0; i < raw.data.size(); ++i) {
        const uint16_t value = raw.data[i];
        const bool in_valid =
            valid.data.size() > i ? valid.data[i] != 0 : true;
        if (!in_valid || value == 0 || value > 1023 ||
            static_cast<std::size_t>(value) >= lut.size()) {
            out.data[i] = 0;
            out_mask.data[i] = 0;
            continue;
        }
        const double calibrated = lut[value];
        double scaled = calibrated;
        switch (unit) {
            case CalUnit::Reflectance:
                scaled = calibrated * 65535.0;
                break;
            case CalUnit::BrightnessTemperature:
                scaled = calibrated;
                break;
            case CalUnit::Radiance:
                scaled = calibrated * 1000.0;
                break;
            case CalUnit::Counts:
                scaled = calibrated;
                break;
        }
        if (scaled < 0.0) {
            scaled = 0.0;
        } else if (scaled > 65535.0) {
            scaled = 65535.0;
        }
        out.data[i] = static_cast<uint16_t>(scaled + 0.5);
        out_mask.data[i] = 1;
    }

    if (out_valid != nullptr) {
        *out_valid = std::move(out_mask);
    }
    return out;
}

}  // namespace ed::image
