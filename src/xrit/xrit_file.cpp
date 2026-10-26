#include "xrit/xrit_file.h"

#include <algorithm>

namespace ed::xrit {

bool XritFile::parse_headers() {
    header_offsets.clear();
    if (data.size() < PrimaryHeader::kSize) {
        return false;
    }
    const PrimaryHeader primary_header(data.data());
    total_header_length = primary_header.total_header_length;

    const std::size_t limit =
        std::min<std::size_t>(total_header_length, data.size());
    std::size_t i = 0;
    while (i + 3 <= limit) {
        const uint8_t type = data[i];
        const uint16_t record_length = read_be16(data.data() + i + 1);
        if (record_length < 3 || i + record_length > limit) {
            break;
        }
        header_offsets[type] = static_cast<uint32_t>(i);
        i += record_length;
    }

    if (has_header(AnnotationRecord::kType)) {
        const AnnotationRecord annotation(
            data.data() + header_offsets[AnnotationRecord::kType]);
        filename = annotation.text;
        const std::size_t nul = filename.find('\0');
        if (nul != std::string::npos) {
            filename.resize(nul);
        }
        std::replace(filename.begin(), filename.end(), '/', '_');
        std::replace(filename.begin(), filename.end(), '\\', '_');
        for (char& c : filename) {
            if (static_cast<unsigned char>(c) < 33) {
                c = '_';
            }
        }
    }
    return true;
}

}  // namespace ed::xrit
