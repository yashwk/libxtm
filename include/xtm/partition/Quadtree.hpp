#pragma once
#include "xtm/partition/Block.hpp"
#include "xtm/analyzer/Selector.hpp"
#include <vector>

namespace xtm::partition {

struct QuadtreeNode {
    BlockView block;
    analyzer::SelectionResult selection;
    bool is_split;
};

class QuadtreePartitioner {
public:
    static std::vector<QuadtreeNode> partition(
        const terrain::IntGrid& grid,
        std::uint32_t max_block_size,
        std::uint32_t min_block_size,
        const analyzer::PredictorSelector& selector,
        double& out_total_bits
    );
};

} // namespace xtm::partition
