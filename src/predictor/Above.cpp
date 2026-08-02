#include "xtm/predictor/Predictors.hpp"

namespace xtm::predictor {

PredictionResult AbovePredictor::encode(const partition::BlockView& block) const {
    PredictionResult result;
    result.residuals.reserve(block.width * block.height);
    
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t X = block.get(x, y);
            int32_t B = (block.global_y(y) > 0) ? block.get_global(block.global_x(x), block.global_y(y) - 1) : 0;
            result.residuals.push_back(X - B);
        }
    }
    return result;
}

void AbovePredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t R = encoded.residuals[y * block.width + x];
            int32_t B = (block.global_y(y) > 0) ? block.get_global(block.global_x(x), block.global_y(y) - 1) : 0;
            block.set(x, y, R + B);
        }
    }
}

} // namespace xtm::predictor
