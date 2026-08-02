#include "xtm/predictor/Predictors.hpp"
#include <cmath>
#include <algorithm>

namespace xtm::predictor {

namespace {
inline int32_t adaptive_gradient_predict(int32_t W, int32_t N, int32_t NW) {
    int32_t dh = std::abs(N - NW);
    int32_t dv = std::abs(W - NW);
    if (dh + dv == 0) return W;
    return (W * dv + N * dh) / (dh + dv);
}
} // namespace

void AdaptiveGradientPredictor::encode(const partition::BlockView& block, PredictionResult& result) const {
    result.residuals.clear();
    result.parameters.clear();
    result.residuals.reserve(block.width * block.height);

    for (uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t val = row[x];
            int32_t p;
            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                p = (x > 0) ? row[x - 1] : 0;
            } else if (x == 0) {
                p = above[0];
            } else {
                p = adaptive_gradient_predict(row[x - 1], above[x], above[x - 1]);
            }
            result.residuals.push_back(val - p);
        }
    }
    return;
}

void AdaptiveGradientPredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    size_t i = 0;
    for (uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t p;
            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                p = (x > 0) ? row[x - 1] : 0;
            } else if (x == 0) {
                p = above[0];
            } else {
                p = adaptive_gradient_predict(row[x - 1], above[x], above[x - 1]);
            }
            row[x] = encoded.residuals[i++] + p;
        }
    }
}

} // namespace xtm::predictor
