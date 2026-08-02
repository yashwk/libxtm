#include "xtm/partition/Quadtree.hpp"
#include <algorithm>

namespace xtm::partition {

namespace {

void partition_recursive(
    const terrain::IntGrid& grid,
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    std::uint32_t min_block_size,
    const analyzer::PredictorSelector& selector,
    std::vector<QuadtreeNode>& leaf_nodes,
    double& accumulated_bits
) {
    if (width == 0 || height == 0) return;

    BlockView parent_block;
    parent_block.grid = &grid;
    parent_block.x_offset = x;
    parent_block.y_offset = y;
    parent_block.width = width;
    parent_block.height = height;

    auto parent_sel = selector.select(parent_block);
    double cost_split_flag = 1.0; // 1 bit overhead for split flag
    
    // If we reached min size or can't split, stop here
    if (width <= min_block_size || height <= min_block_size) {
        leaf_nodes.push_back({parent_block, std::move(parent_sel), false});
        accumulated_bits += parent_sel.total_bits + cost_split_flag;
        return;
    }

    std::uint32_t w1 = width / 2;
    std::uint32_t w2 = width - w1;
    std::uint32_t h1 = height / 2;
    std::uint32_t h2 = height - h1;

    std::vector<QuadtreeNode> temp_leaves;
    double children_bits = 0.0;
    
    if (w1 > 0 && h1 > 0)
        partition_recursive(grid, x, y, w1, h1, min_block_size, selector, temp_leaves, children_bits);
    if (w2 > 0 && h1 > 0)
        partition_recursive(grid, x + w1, y, w2, h1, min_block_size, selector, temp_leaves, children_bits);
    if (w1 > 0 && h2 > 0)
        partition_recursive(grid, x, y + h1, w1, h2, min_block_size, selector, temp_leaves, children_bits);
    if (w2 > 0 && h2 > 0)
        partition_recursive(grid, x + w1, y + h1, w2, h2, min_block_size, selector, temp_leaves, children_bits);

    if (children_bits + cost_split_flag < parent_sel.total_bits + cost_split_flag) {
        // Splitting is cheaper
        leaf_nodes.insert(leaf_nodes.end(), temp_leaves.begin(), temp_leaves.end());
        accumulated_bits += children_bits + cost_split_flag;
    } else {
        // Parent is cheaper
        leaf_nodes.push_back({parent_block, std::move(parent_sel), false});
        accumulated_bits += parent_sel.total_bits + cost_split_flag;
    }
}

} // namespace

std::vector<QuadtreeNode> QuadtreePartitioner::partition(
    const terrain::IntGrid& grid,
    std::uint32_t max_block_size,
    std::uint32_t min_block_size,
    const analyzer::PredictorSelector& selector,
    double& out_total_bits
) {
    std::vector<QuadtreeNode> leaf_nodes;
    out_total_bits = 0.0;
    
    for (std::uint32_t y = 0; y < grid.height; y += max_block_size) {
        for (std::uint32_t x = 0; x < grid.width; x += max_block_size) {
            std::uint32_t w = std::min(max_block_size, grid.width - x);
            std::uint32_t h = std::min(max_block_size, grid.height - y);
            partition_recursive(grid, x, y, w, h, min_block_size, selector, leaf_nodes, out_total_bits);
        }
    }
    
    return leaf_nodes;
}

} // namespace xtm::partition
