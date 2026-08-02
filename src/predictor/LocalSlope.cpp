#include "xtm/predictor/Predictors.hpp"
#include <cmath>

namespace xtm::predictor {

PredictionResult LocalSlopePredictor::encode(const partition::BlockView& block) const {
    PredictionResult result;
    result.residuals.reserve(block.width * block.height);

    for (uint32_t y = 0; y < block.height; ++y) {
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t val = block.get(x, y);
            int32_t p = 0;

            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                int32_t W = block.get(x - 1, 0);
                int32_t WW = (x >= 2) ? block.get(x - 2, 0) : W;
                p = W + (W - WW);
            } else if (x == 0) {
                int32_t N = block.get(0, y - 1);
                int32_t NN = (y >= 2) ? block.get(0, y - 2) : N;
                p = N + (N - NN);
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t NW = block.get(x - 1, y - 1);
                
                int32_t WW = (x >= 2) ? block.get(x - 2, y) : W;
                int32_t NN = (y >= 2) ? block.get(x, y - 2) : N;
                
                int32_t NWW = (x >= 2) ? block.get(x - 2, y - 1) : NW;
                int32_t NNW = (y >= 2) ? block.get(x - 1, y - 2) : NW;
                
                int32_t dx1 = W - WW;
                int32_t dx2 = NW - NWW;
                int32_t dx = (dx1 + dx2) / 2;
                
                int32_t dy1 = N - NN;
                int32_t dy2 = NW - NNW;
                int32_t dy = (dy1 + dy2) / 2;
                
                p = (W + dx + N + dy) / 2;
            }

            result.residuals.push_back(val - p);
        }
    }
    return result;
}

void LocalSlopePredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    size_t i = 0;
    for (uint32_t y = 0; y < block.height; ++y) {
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t res = encoded.residuals[i++];
            int32_t p = 0;

            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                int32_t W = block.get(x - 1, 0);
                int32_t WW = (x >= 2) ? block.get(x - 2, 0) : W;
                p = W + (W - WW);
            } else if (x == 0) {
                int32_t N = block.get(0, y - 1);
                int32_t NN = (y >= 2) ? block.get(0, y - 2) : N;
                p = N + (N - NN);
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t NW = block.get(x - 1, y - 1);
                
                int32_t WW = (x >= 2) ? block.get(x - 2, y) : W;
                int32_t NN = (y >= 2) ? block.get(x, y - 2) : N;
                
                int32_t NWW = (x >= 2) ? block.get(x - 2, y - 1) : NW;
                int32_t NNW = (y >= 2) ? block.get(x - 1, y - 2) : NW;
                
                int32_t dx1 = W - WW;
                int32_t dx2 = NW - NWW;
                int32_t dx = (dx1 + dx2) / 2;
                
                int32_t dy1 = N - NN;
                int32_t dy2 = NW - NNW;
                int32_t dy = (dy1 + dy2) / 2;
                
                p = (W + dx + N + dy) / 2;
            }

            block.set(x, y, p + res);
        }
    }
}

} // namespace xtm::predictor
