#include "xtm/predictor/Predictors.hpp"

namespace xtm::predictor {

void AbovePredictor::encode(const partition::BlockView& block, PredictionResult& result) const {
    result.residuals.clear();
    result.parameters.clear();
    result.residuals.reserve(block.width * block.height);
    
    const bool has_above_neighbor = block.global_y(0) > 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t B = above ? above[x] : (has_above_neighbor ? block.get_global(block.global_x(x), block.global_y(0) - 1) : 0);
            result.residuals.push_back(row[x] - B);
        }
    }
    return;
}

void AbovePredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    const bool has_above_neighbor = block.global_y(0) > 0;
    size_t i = 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t B = above ? above[x] : (has_above_neighbor ? block.get_global(block.global_x(x), block.global_y(0) - 1) : 0);
            row[x] = encoded.residuals[i++] + B;
        }
    }
}

} // namespace xtm::predictor
