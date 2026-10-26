// ElektroDump — GGAK SKL 14x14 science-record de-quantisation (PLAN.md D2).
// Small self-contained utility used by the offline `ggak decode` command and
// the tests; not on the live hot path.
#pragma once

#include <vector>

#include "ggak/ggak_frame.h"

namespace ed::ggak {

struct GgakSklRecord {
    double time = 0.0;
    double dat[12] = {0.0};
};

// Parse the 14 records carried by an id==48 frame; empty for any other id.
std::vector<GgakSklRecord> parse_skl(const GgakFrame& frame);

// Average runs of 10 one-second records into 10-second records.
std::vector<GgakSklRecord> resample_skl_10s(
    const std::vector<GgakSklRecord>& records);

}  // namespace ed::ggak
