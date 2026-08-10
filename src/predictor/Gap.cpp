#include "xtm/predictor/Predictors.hpp"
#include <cmath>
#include <algorithm>

namespace xtm::predictor {

namespace {
inline int32_t gap_predict(int32_t W, int32_t N, int32_t NW, int32_t NE,
                           int32_t WW, int32_t NN, int32_t NNE) {
    int32_t dv = std::abs(W - NW) + std::abs(N - NN) + std::abs(NE - NNE);
    int32_t dh = std::abs(W - WW) + std::abs(N - NW) + std::abs(N - NE);

    int32_t diff = dv - dh;
    int32_t p = (W + N) / 2 + (NE - NW) / 4;

    if (diff > 80) p = W;
    else if (diff < -80) p = N;
    else {
        if (diff > 32) p = (p + W) / 2;
        else if (diff > 8) p = (3 * p + W) / 4;
        else if (diff < -32) p = (p + N) / 2;
        else if (diff < -8) p = (3 * p + N) / 4;
    }
    return p;
}
} // namespace

void GapPredictor::encode(const partition::BlockView& block, std::vector<int32_t>& residuals, std::vector<int32_t>& parameters) const {
    residuals.clear();
    parameters.clear();
    residuals.reserve(block.width * block.height);

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
                p = row[x - 1];
            } else if (x == 0) {
                p = above[0];
            } else {
                int32_t W = row[x - 1];
                int32_t N = above[x];
                int32_t NW = above[x - 1];
                int32_t NE = (x + 1 < block.width) ? above[x + 1] : N;

                int32_t WW = (x >= 2) ? row[x - 2] : W;
                int32_t NN = (y >= 2) ? above2[x] : N;
                int32_t NNE = (x + 1 < block.width && y >= 2) ? above2[x + 1] : NE;

                p = gap_predict(W, N, NW, NE, WW, NN, NNE);
            }

            residuals.push_back(val - p);
        }
    }
    return;
}

void GapPredictor::decode(const std::vector<int32_t>& residuals, const std::vector<int32_t>& /*parameters*/, partition::MutableBlockView& block) const {
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
                p = row[x - 1];
            } else if (x == 0) {
                p = above[0];
            } else {
                int32_t W = row[x - 1];
                int32_t N = above[x];
                int32_t NW = above[x - 1];
                int32_t NE = (x + 1 < block.width) ? above[x + 1] : N;

                int32_t WW = (x >= 2) ? row[x - 2] : W;
                int32_t NN = (y >= 2) ? above2[x] : N;
                int32_t NNE = (x + 1 < block.width && y >= 2) ? above2[x + 1] : NE;

                p = gap_predict(W, N, NW, NE, WW, NN, NNE);
            }

            row[x] = residuals[i++] + p;
        }
    }
}

} // namespace xtm::predictor
