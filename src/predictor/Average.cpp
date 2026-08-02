#include "xtm/predictor/Predictors.hpp"

namespace xtm::predictor {

void AveragePredictor::encode(const partition::BlockView& block, PredictionResult& result) const {
    result.residuals.clear();
    result.parameters.clear();
    result.residuals.reserve(block.width * block.height);
    
    const bool has_left_neighbor = block.global_x(0) > 0;
    const bool has_above_neighbor = block.global_y(0) > 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        int32_t C = has_left_neighbor ? block.get_global(block.global_x(0) - 1, block.global_y(y)) : 0;
        for (std::uint32_t x = 0; x < block.width; ++x) {
            if (x > 0) C = row[x - 1];
            int32_t B = above ? above[x] : (has_above_neighbor ? block.get_global(block.global_x(x), block.global_y(0) - 1) : 0);
            int32_t P = (B + C) / 2;
            result.residuals.push_back(row[x] - P);
        }
    }
    return;
}

void AveragePredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    const bool has_left_neighbor = block.global_x(0) > 0;
    const bool has_above_neighbor = block.global_y(0) > 0;
    size_t i = 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        int32_t C = has_left_neighbor ? block.get_global(block.global_x(0) - 1, block.global_y(y)) : 0;
        for (std::uint32_t x = 0; x < block.width; ++x) {
            if (x > 0) C = row[x - 1];
            int32_t B = above ? above[x] : (has_above_neighbor ? block.get_global(block.global_x(x), block.global_y(0) - 1) : 0);
            int32_t P = (B + C) / 2;
            row[x] = encoded.residuals[i++] + P;
        }
    }
}

} // namespace xtm::predictor
