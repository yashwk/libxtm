#include "xtm/predictor/Predictors.hpp"
#include <cmath>
#include <algorithm>

namespace xtm::predictor {

PredictionResult AdaptiveGradientPredictor::encode(const partition::BlockView& block) const {
    PredictionResult result;
    result.residuals.reserve(block.width * block.height);

    for (uint32_t y = 0; y < block.height; ++y) {
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t val = block.get(x, y);
            int32_t p = 0;

            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                p = block.get(x - 1, 0);
            } else if (x == 0) {
                p = block.get(0, y - 1);
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t NW = block.get(x - 1, y - 1);
                
                int32_t dh = std::abs(N - NW);
                int32_t dv = std::abs(W - NW);
                
                if (dh + dv == 0) {
                    p = W;
                } else {
                    // If dh is large (horizontal edge), we want to predict from N
                    // If dv is large (vertical edge), we want to predict from W
                    p = (W * dv + N * dh) / (dh + dv);
                }
            }

            result.residuals.push_back(val - p);
        }
    }
    return result;
}

void AdaptiveGradientPredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    size_t i = 0;
    for (uint32_t y = 0; y < block.height; ++y) {
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t res = encoded.residuals[i++];
            int32_t p = 0;

            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                p = block.get(x - 1, 0);
            } else if (x == 0) {
                p = block.get(0, y - 1);
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t NW = block.get(x - 1, y - 1);
                
                int32_t dh = std::abs(N - NW);
                int32_t dv = std::abs(W - NW);
                
                if (dh + dv == 0) {
                    p = W;
                } else {
                    p = (W * dv + N * dh) / (dh + dv);
                }
            }

            block.set(x, y, p + res);
        }
    }
}

} // namespace xtm::predictor
