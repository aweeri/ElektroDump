#include "core/pipeline.h"

namespace ed {

const char* pipeline_mode_name(PipelineMode mode) {
    switch (mode) {
        case PipelineMode::GGAK: return "ggak";
        case PipelineMode::LRIT: return "lrit";
        case PipelineMode::HRIT: return "hrit";
    }
    return "unknown";
}

}  // namespace ed
