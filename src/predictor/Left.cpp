#include "xtm/predictor/Predictors.hpp"

namespace xtm::predictor {

void LeftPredictor::encode(const partition::BlockView& block, std::vector<int32_t>& residuals, std::vector<int32_t>& parameters) const {
    residuals.clear();
    parameters.clear();
    residuals.reserve(block.width * block.height);
    
    const bool has_left_neighbor = block.global_x(0) > 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        int32_t C = has_left_neighbor ? block.get_global(block.global_x(0) - 1, block.global_y(y)) : 0;
        residuals.push_back(row[0] - C);
        for (std::uint32_t x = 1; x < block.width; ++x) {
            residuals.push_back(row[x] - row[x - 1]);
        }
    }
    return;
}

void LeftPredictor::decode(const std::vector<int32_t>& residuals, const std::vector<int32_t>& /*parameters*/, partition::MutableBlockView& block) const {
    const bool has_left_neighbor = block.global_x(0) > 0;
    size_t i = 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        int32_t C = has_left_neighbor ? block.get_global(block.global_x(0) - 1, block.global_y(y)) : 0;
        row[0] = residuals[i++] + C;
        for (std::uint32_t x = 1; x < block.width; ++x) {
            row[x] = residuals[i++] + row[x - 1];
        }
    }
}

} // namespace xtm::predictor
