// ElektroDump — xRIT header records (PLAN.md D3.9, Appendix A.3).
// Every record starts with a 1-byte type and a 2-byte big-endian length (which
// includes those 3 bytes). Parsing is bounds-safe: constructors never read past
// the caller-provided record length.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ed::xrit {

inline uint16_t read_be16(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline uint64_t read_be64(const uint8_t* p) {
    return (static_cast<uint64_t>(read_be32(p)) << 32) |
           static_cast<uint64_t>(read_be32(p + 4));
}

struct PrimaryHeader {
    static constexpr uint8_t kType = 0;
    static constexpr std::size_t kSize = 16;

    uint8_t type = 0;
    uint16_t record_length = 0;
    uint8_t file_type_code = 0;
    uint32_t total_header_length = 0;
    uint64_t data_field_length = 0;

    explicit PrimaryHeader(const uint8_t* data) {
        type = data[0];
        record_length = read_be16(data + 1);
        file_type_code = data[3];
        total_header_length = read_be32(data + 4);
        data_field_length = read_be64(data + 8);
    }
    PrimaryHeader() = default;
};

struct ImageStructureRecord {
    static constexpr uint8_t kType = 1;

    uint8_t type = 0;
    uint16_t record_length = 0;
    uint8_t bits_per_pixel = 0;
    uint16_t columns_count = 0;
    uint16_t lines_count = 0;
    uint8_t compression_flag = 0;

    explicit ImageStructureRecord(const uint8_t* data) {
        type = data[0];
        record_length = read_be16(data + 1);
        bits_per_pixel = data[3];
        columns_count = read_be16(data + 4);
        lines_count = read_be16(data + 6);
        compression_flag = data[8];
    }
    ImageStructureRecord() = default;
};

struct ImageNavigationRecord {
    static constexpr uint8_t kType = 2;

    uint8_t type = 0;
    uint16_t record_length = 0;
    std::string projection_name;  // 32 bytes, e.g. "geos(76.0)"
    int32_t column_scaling_factor = 0;
    int32_t line_scaling_factor = 0;
    double column_offset = 0.0;
    double line_offset = 0.0;

    explicit ImageNavigationRecord(const uint8_t* data) {
        type = data[0];
        record_length = read_be16(data + 1);
        projection_name.assign(reinterpret_cast<const char*>(data + 3), 32);
        const std::size_t nul = projection_name.find('\0');
        if (nul != std::string::npos) {
            projection_name.resize(nul);
        }
        column_scaling_factor =
            static_cast<int32_t>(read_be32(data + 35));
        line_scaling_factor = static_cast<int32_t>(read_be32(data + 39));
        column_offset = static_cast<double>(
            static_cast<int32_t>(read_be32(data + 43)));
        line_offset =
            static_cast<double>(static_cast<int32_t>(read_be32(data + 47)));
    }
    ImageNavigationRecord() = default;
};

struct ImageDataFunctionRecord {
    static constexpr uint8_t kType = 3;

    uint8_t type = 0;
    uint16_t record_length = 0;
    std::string data;

    explicit ImageDataFunctionRecord(const uint8_t* raw) {
        type = raw[0];
        record_length = read_be16(raw + 1);
        if (record_length > 3) {
            data.assign(reinterpret_cast<const char*>(raw + 3),
                        record_length - 3);
        }
    }
    ImageDataFunctionRecord() = default;
};

struct AnnotationRecord {
    static constexpr uint8_t kType = 4;

    uint8_t type = 0;
    uint16_t record_length = 0;
    std::string text;

    explicit AnnotationRecord(const uint8_t* raw) {
        type = raw[0];
        record_length = read_be16(raw + 1);
        if (record_length > 3) {
            text.assign(reinterpret_cast<const char*>(raw + 3),
                        record_length - 3);
        }
    }
    AnnotationRecord() = default;
};

struct TimeStampRecord {
    static constexpr uint8_t kType = 5;

    uint8_t type = 0;
    uint16_t record_length = 0;
    uint16_t days = 0;
    uint32_t milliseconds_of_day = 0;

    explicit TimeStampRecord(const uint8_t* data) {
        type = data[0];
        record_length = read_be16(data + 1);
        days = read_be16(data + 3);
        milliseconds_of_day = read_be32(data + 5);
    }
    TimeStampRecord() = default;
};

// Elektro-L segment identification record.
struct SegmentIdentificationRecord {
    static constexpr uint8_t kType = 128;

    uint8_t type = 0;
    uint16_t record_length = 0;
    uint16_t sc_id = 0;
    uint8_t channel_id = 0;
    uint16_t segment_sequence_number = 0;
    uint16_t planned_start_segment = 0;
    uint16_t planned_end_segment = 0;
    uint8_t compression = 0;

    explicit SegmentIdentificationRecord(const uint8_t* data) {
        type = data[0];
        record_length = read_be16(data + 1);
        sc_id = read_be16(data + 3);
        channel_id = data[5];
        segment_sequence_number = read_be16(data + 6);
        planned_start_segment = read_be16(data + 8);
        planned_end_segment = read_be16(data + 10);
        compression = data[12];
    }
    SegmentIdentificationRecord() = default;
};

}  // namespace ed::xrit
