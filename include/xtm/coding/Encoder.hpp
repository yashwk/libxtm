#pragma once
#include "xtm/terrain/Quantization.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include <map>
#include <cstdint>

namespace xtm::coding {

struct PredictorStats {
    uint32_t count = 0;
};

struct EncodeResult {
    std::map<uint32_t, PredictorStats> predictor_stats;
    uint32_t total_blocks = 0;
    
    double time_quadtree = 0.0;
    double time_entropy = 0.0;
    double time_io = 0.0;
};

class XtmEncoder {
public:
    static EncodeResult encode(const terrain::IntGrid& grid,
                               container::XtmWriter& writer,
                               const PipelineContext& ctx);
};

} // namespace xtm::coding
