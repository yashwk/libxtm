#include "xtm/predictor/Predictors.hpp"
#include <cmath>

namespace xtm::predictor {

void LocalSlopePredictor::encode(const partition::BlockView& block, PredictionResult& result) const {
    result.residuals.clear();
    result.parameters.clear();
    result.residuals.reserve(block.width * block.height);

    for (uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        const int32_t* above2 = (y > 1) ? block.row_data(y - 2) : nullptr;
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t val = row[x];
            int32_t p;

            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                int32_t W = row[x - 1];
                int32_t WW = (x >= 2) ? row[x - 2] : W;
                p = W + (W - WW);
            } else if (x == 0) {
                int32_t N = above[0];
                int32_t NN = (y >= 2) ? above2[0] : N;
                p = N + (N - NN);
            } else {
                int32_t W = row[x - 1];
                int32_t N = above[x];
                int32_t NW = above[x - 1];

                int32_t WW = (x >= 2) ? row[x - 2] : W;
                int32_t NN = (y >= 2) ? above2[x] : N;

                int32_t NWW = (x >= 2) ? above[x - 2] : NW;
                int32_t NNW = (y >= 2) ? above2[x - 1] : NW;

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
    return;
}

void LocalSlopePredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    size_t i = 0;
    for (uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        const int32_t* above2 = (y > 1) ? block.row_data(y - 2) : nullptr;
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t p;

            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                int32_t W = row[x - 1];
                int32_t WW = (x >= 2) ? row[x - 2] : W;
                p = W + (W - WW);
            } else if (x == 0) {
                int32_t N = above[0];
                int32_t NN = (y >= 2) ? above2[0] : N;
                p = N + (N - NN);
            } else {
                int32_t W = row[x - 1];
                int32_t N = above[x];
                int32_t NW = above[x - 1];

                int32_t WW = (x >= 2) ? row[x - 2] : W;
                int32_t NN = (y >= 2) ? above2[x] : N;

                int32_t NWW = (x >= 2) ? above[x - 2] : NW;
                int32_t NNW = (y >= 2) ? above2[x - 1] : NW;

                int32_t dx1 = W - WW;
                int32_t dx2 = NW - NWW;
                int32_t dx = (dx1 + dx2) / 2;

                int32_t dy1 = N - NN;
                int32_t dy2 = NW - NNW;
                int32_t dy = (dy1 + dy2) / 2;

                p = (W + dx + N + dy) / 2;
            }

            row[x] = encoded.residuals[i++] + p;
        }
    }
}

} // namespace xtm::predictor
