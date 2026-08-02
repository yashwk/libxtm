#include "xtm/predictor/Predictors.hpp"
#include <algorithm>

namespace xtm::predictor {

PredictionResult JpegLsPredictor::encode(const partition::BlockView& block) const {
    PredictionResult result;
    result.residuals.reserve(block.width * block.height);
    
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t X = block.get(x, y);
            int32_t A = (block.global_x(x) > 0 && block.global_y(y) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y) - 1) : 0;
            int32_t B = (block.global_y(y) > 0) ? block.get_global(block.global_x(x), block.global_y(y) - 1) : 0;
            int32_t C = (block.global_x(x) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y)) : 0;
            
            int32_t P;
            if (A >= std::max(B, C)) {
                P = std::min(B, C);
            } else if (A <= std::min(B, C)) {
                P = std::max(B, C);
            } else {
                P = B + C - A;
            }
            
            result.residuals.push_back(X - P);
        }
    }
    return result;
}

void JpegLsPredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t R = encoded.residuals[y * block.width + x];
            int32_t A = (block.global_x(x) > 0 && block.global_y(y) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y) - 1) : 0;
            int32_t B = (block.global_y(y) > 0) ? block.get_global(block.global_x(x), block.global_y(y) - 1) : 0;
            int32_t C = (block.global_x(x) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y)) : 0;
            
            int32_t P;
            if (A >= std::max(B, C)) {
                P = std::min(B, C);
            } else if (A <= std::min(B, C)) {
                P = std::max(B, C);
            } else {
                P = B + C - A;
            }
            
            block.set(x, y, R + P);
        }
    }
}

} // namespace xtm::predictor
