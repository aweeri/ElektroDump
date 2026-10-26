// ElektroDump — assembled xRIT file (PLAN.md D3.9).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "xrit/xrit_headers.h"

namespace ed::xrit {

struct XritFile {
    int vcid = -1;
    uint16_t apid = 0;
    bool file_in_progress = false;
    bool header_parsed = false;
    bool crc_recovered = false;

    std::string filename;
    uint32_t total_header_length = 0;
    std::map<uint8_t, uint32_t> header_offsets;  // record type -> byte offset

    // Assembled file: header records followed by the data field.
    std::vector<uint8_t> data;

    bool has_header(uint8_t type) const {
        return header_offsets.find(type) != header_offsets.end();
    }

    // Parse the header records out of `data`; true when the primary header was
    // read and the total header length is known.
    bool parse_headers();

    PrimaryHeader primary() const {
        return PrimaryHeader(data.data());
    }

    ImageStructureRecord image_structure() const {
        return ImageStructureRecord(data.data() + header_offsets.at(
                                                         ImageStructureRecord::kType));
    }

    SegmentIdentificationRecord segment_identification() const {
        return SegmentIdentificationRecord(
            data.data() +
            header_offsets.at(SegmentIdentificationRecord::kType));
    }

    void reset() {
        vcid = -1;
        apid = 0;
        file_in_progress = false;
        header_parsed = false;
        crc_recovered = false;
        filename.clear();
        total_header_length = 0;
        header_offsets.clear();
        data.clear();
    }
};

}  // namespace ed::xrit
