#pragma once
#include "xtm/terrain/Quantization.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include <cstdint>

namespace xtm::coding {

struct DecodeResult {
    uint32_t blocks_decoded = 0;
};

class XtmDecoder {
public:
    static DecodeResult decode(container::XtmReader& reader,
                               terrain::IntGrid& roi_grid,
                               int rx, int ry, int rw, int rh,
                               uint32_t num_threads = 0);
};

} // namespace xtm::coding
