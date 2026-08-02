#pragma once
#include "xtm/terrain/Quantization.hpp"
#include <vector>
#include <cstdint>

#include "xtm/partition/Block.hpp"

namespace xtm::predictor {

struct PredictionResult {
    std::vector<int32_t> residuals;
    std::vector<int32_t> parameters;
};

class Predictor {
public:
    virtual ~Predictor() = default;

    virtual void encode(const partition::BlockView& block, PredictionResult& out_result) const = 0;
    virtual void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const = 0;
    
    virtual const char* name() const = 0;
};

} // namespace xtm::predictor
