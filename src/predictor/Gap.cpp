#include "xtm/predictor/Predictors.hpp"
#include <cmath>
#include <algorithm>

namespace xtm::predictor {

PredictionResult GapPredictor::encode(const partition::BlockView& block) const {
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
                int32_t NE = (x + 1 < block.width) ? block.get(x + 1, y - 1) : N;
                
                int32_t WW = (x >= 2) ? block.get(x - 2, y) : W;
                int32_t NN = (y >= 2) ? block.get(x, y - 2) : N;
                int32_t NNE = (x + 1 < block.width && y >= 2) ? block.get(x + 1, y - 2) : NE;
                
                int32_t dv = std::abs(W - NW) + std::abs(N - NN) + std::abs(NE - NNE);
                int32_t dh = std::abs(W - WW) + std::abs(N - NW) + std::abs(N - NE);
                
                int32_t diff = dv - dh;
                p = (W + N) / 2 + (NE - NW) / 4;
                
                if (diff > 80) p = W;
                else if (diff < -80) p = N;
                else {
                    if (diff > 32) p = (p + W) / 2;
                    else if (diff > 8) p = (3 * p + W) / 4;
                    else if (diff < -32) p = (p + N) / 2;
                    else if (diff < -8) p = (3 * p + N) / 4;
                }
            }

            result.residuals.push_back(val - p);
        }
    }
    return result;
}

void GapPredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
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
                int32_t NE = (x + 1 < block.width) ? block.get(x + 1, y - 1) : N;
                
                int32_t WW = (x >= 2) ? block.get(x - 2, y) : W;
                int32_t NN = (y >= 2) ? block.get(x, y - 2) : N;
                int32_t NNE = (x + 1 < block.width && y >= 2) ? block.get(x + 1, y - 2) : NE;
                
                int32_t dv = std::abs(W - NW) + std::abs(N - NN) + std::abs(NE - NNE);
                int32_t dh = std::abs(W - WW) + std::abs(N - NW) + std::abs(N - NE);
                
                int32_t diff = dv - dh;
                p = (W + N) / 2 + (NE - NW) / 4;
                
                if (diff > 80) p = W;
                else if (diff < -80) p = N;
                else {
                    if (diff > 32) p = (p + W) / 2;
                    else if (diff > 8) p = (3 * p + W) / 4;
                    else if (diff < -32) p = (p + N) / 2;
                    else if (diff < -8) p = (3 * p + N) / 4;
                }
            }

            block.set(x, y, p + res);
        }
    }
}

} // namespace xtm::predictor
