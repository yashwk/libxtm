#pragma once
#include "xtm/terrain/Quantization.hpp"
#include <cstdint>
#include <vector>
#include <algorithm>

namespace xtm::partition {

struct BlockView {
    const terrain::IntGrid* grid;
    std::uint32_t x_offset;
    std::uint32_t y_offset;
    std::uint32_t width;
    std::uint32_t height;

    int32_t get(std::uint32_t local_x, std::uint32_t local_y) const {
        return grid->get(x_offset + local_x, y_offset + local_y);
    }
    
    // Raw pointer to the start of the block's row (local_y) for fast sequential access
    const int32_t* row_data(std::uint32_t local_y) const {
        return grid->data.data() + (y_offset + local_y) * grid->width + x_offset;
    }
    
    // Returns value from global grid, allowing predictors to look across boundaries (e.g., A, B, C neighbors)
    int32_t get_global(std::uint32_t global_x, std::uint32_t global_y) const {
        return grid->get(global_x, global_y);
    }
    
    std::uint32_t global_x(std::uint32_t local_x) const { return x_offset + local_x; }
    std::uint32_t global_y(std::uint32_t local_y) const { return y_offset + local_y; }
};

struct MutableBlockView {
    terrain::IntGrid* grid;
    std::uint32_t x_offset;
    std::uint32_t y_offset;
    std::uint32_t width;
    std::uint32_t height;

    int32_t get(std::uint32_t local_x, std::uint32_t local_y) const {
        return grid->get(x_offset + local_x, y_offset + local_y);
    }
    
    int32_t get_global(std::uint32_t global_x, std::uint32_t global_y) const {
        return grid->get(global_x, global_y);
    }
    
    // Raw pointer to the start of the block's row (local_y) for fast sequential access
    int32_t* row_data(std::uint32_t local_y) {
        return grid->data.data() + (y_offset + local_y) * grid->width + x_offset;
    }
    
    void set(std::uint32_t local_x, std::uint32_t local_y, int32_t val) {
        grid->data[(y_offset + local_y) * grid->width + (x_offset + local_x)] = val;
    }
    
    std::uint32_t global_x(std::uint32_t local_x) const { return x_offset + local_x; }
    std::uint32_t global_y(std::uint32_t local_y) const { return y_offset + local_y; }
};

class FixedGridPartitioner {
public:
    static std::vector<BlockView> partition(const terrain::IntGrid& grid, std::uint32_t block_size) {
        std::vector<BlockView> blocks;
        for (std::uint32_t y = 0; y < grid.height; y += block_size) {
            for (std::uint32_t x = 0; x < grid.width; x += block_size) {
                BlockView block;
                block.grid = &grid;
                block.x_offset = x;
                block.y_offset = y;
                block.width = std::min(block_size, grid.width - x);
                block.height = std::min(block_size, grid.height - y);
                blocks.push_back(block);
            }
        }
        return blocks;
    }
    
    static std::vector<MutableBlockView> partition(terrain::IntGrid& grid, std::uint32_t block_size) {
        std::vector<MutableBlockView> blocks;
        for (std::uint32_t y = 0; y < grid.height; y += block_size) {
            for (std::uint32_t x = 0; x < grid.width; x += block_size) {
                MutableBlockView block;
                block.grid = &grid;
                block.x_offset = x;
                block.y_offset = y;
                block.width = std::min(block_size, grid.width - x);
                block.height = std::min(block_size, grid.height - y);
                blocks.push_back(block);
            }
        }
        return blocks;
    }
};

} // namespace xtm::partition
