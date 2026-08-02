#include "xtm/predictor/Predictors.hpp"

namespace xtm::predictor {

PredictionResult SecondOrderPredictor::encode(const partition::BlockView& block) const {
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
                p = 2 * W - WW;
            } else if (x == 0) {
                int32_t N = block.get(0, y - 1);
                int32_t NN = (y >= 2) ? block.get(0, y - 2) : N;
                p = 2 * N - NN;
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t WW = (x >= 2) ? block.get(x - 2, y) : W;
                int32_t NN = (y >= 2) ? block.get(x, y - 2) : N;
                
                p = W - WW / 2 + N - NN / 2;
            }

            result.residuals.push_back(val - p);
        }
    }
    return result;
}

void SecondOrderPredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
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
                p = 2 * W - WW;
            } else if (x == 0) {
                int32_t N = block.get(0, y - 1);
                int32_t NN = (y >= 2) ? block.get(0, y - 2) : N;
                p = 2 * N - NN;
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t WW = (x >= 2) ? block.get(x - 2, y) : W;
                int32_t NN = (y >= 2) ? block.get(x, y - 2) : N;
                
                p = W - WW / 2 + N - NN / 2;
            }

            block.set(x, y, p + res);
        }
    }
}

} // namespace xtm::predictor
