#include "xtm/predictor/Predictors.hpp"
#include <algorithm>

namespace xtm::predictor {

namespace {
inline int32_t jpegls_predict(int32_t A, int32_t B, int32_t C) {
    if (A >= std::max(B, C)) {
        return std::min(B, C);
    } else if (A <= std::min(B, C)) {
        return std::max(B, C);
    }
    return B + C - A;
}
} // namespace

void JpegLsPredictor::encode(const partition::BlockView& block, PredictionResult& result) const {
    result.residuals.clear();
    result.parameters.clear();
    result.residuals.reserve(block.width * block.height);
    
    for (std::uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t P;
            if (above && x > 0) {
                P = jpegls_predict(above[x - 1], above[x], row[x - 1]);
            } else {
                int32_t A = (block.global_x(x) > 0 && block.global_y(y) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y) - 1) : 0;
                int32_t B = (block.global_y(y) > 0) ? block.get_global(block.global_x(x), block.global_y(y) - 1) : 0;
                int32_t C = (block.global_x(x) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y)) : 0;
                P = jpegls_predict(A, B, C);
            }
            result.residuals.push_back(row[x] - P);
        }
    }
    return;
}

void JpegLsPredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    size_t i = 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t P;
            if (above && x > 0) {
                P = jpegls_predict(above[x - 1], above[x], row[x - 1]);
            } else {
                int32_t A = (block.global_x(x) > 0 && block.global_y(y) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y) - 1) : 0;
                int32_t B = (block.global_y(y) > 0) ? block.get_global(block.global_x(x), block.global_y(y) - 1) : 0;
                int32_t C = (block.global_x(x) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y)) : 0;
                P = jpegls_predict(A, B, C);
            }
            row[x] = encoded.residuals[i++] + P;
        }
    }
}

} // namespace xtm::predictor
