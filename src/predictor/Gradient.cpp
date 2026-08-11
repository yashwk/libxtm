#include "xtm/predictor/Predictors.hpp"

namespace xtm::predictor {

void GradientPredictor::encode(const partition::BlockView& block, std::vector<int32_t>& residuals, std::vector<int32_t>& parameters) const {
    parameters.clear();
    residuals.assign(block.width * block.height, 0);
    
    // First row (y == 0): context comes from the global grid (the block may
    // not touch the grid's top edge), scalar.
    for (std::uint32_t x = 0; x < block.width; ++x) {
        int32_t A = (block.global_x(x) > 0 && block.global_y(0) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(0) - 1) : 0;
        int32_t B = (block.global_y(0) > 0) ? block.get_global(block.global_x(x), block.global_y(0) - 1) : 0;
        int32_t C = (block.global_x(x) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(0)) : 0;
        residuals[x] = block.row_data(0)[x] - (B + C - A);
    }
    
    // First column (x == 0): left neighbor comes from the global grid.
    for (std::uint32_t y = 1; y < block.height; ++y) {
        int32_t A = (block.global_x(0) > 0) ? block.get_global(block.global_x(0) - 1, block.global_y(y) - 1) : 0;
        int32_t B = block.get_global(block.global_x(0), block.global_y(y) - 1);
        int32_t C = (block.global_x(0) > 0) ? block.get_global(block.global_x(0) - 1, block.global_y(y)) : 0;
        residuals[y * block.width] = block.row_data(y)[0] - (B + C - A);
    }
    
    // Interior: purely local causal context; no loop-carried dependency
    // (reads the original row/above, writes residuals), so the compiler
    // can vectorize this loop.
    const std::uint32_t w = block.width;
    const std::uint32_t h = block.height;
    for (std::uint32_t y = 1; y < h; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = block.row_data(y - 1);
#if defined(__clang__)
#pragma clang loop vectorize(enable)
#else
#pragma GCC ivdep
#endif
        for (std::uint32_t x = 1; x < w; ++x) {
            residuals[y * w + x] = row[x] - (above[x] + row[x - 1] - above[x - 1]);
        }
    }
    return;
}

void GradientPredictor::decode(const std::vector<int32_t>& residuals, const std::vector<int32_t>& /*parameters*/, partition::MutableBlockView& block) const {
    size_t i = 0;
    for (std::uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t P;
            if (above && x > 0) {
                P = above[x] + row[x - 1] - above[x - 1];
            } else {
                int32_t A = (block.global_x(x) > 0 && block.global_y(y) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y) - 1) : 0;
                int32_t B = (block.global_y(y) > 0) ? block.get_global(block.global_x(x), block.global_y(y) - 1) : 0;
                int32_t C = (block.global_x(x) > 0) ? block.get_global(block.global_x(x) - 1, block.global_y(y)) : 0;
                P = B + C - A;
            }
            row[x] = residuals[i++] + P;
        }
    }
}

} // namespace xtm::predictor
