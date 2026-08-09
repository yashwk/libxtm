#pragma once
#include "xtm/Terrain.hpp"
#include "xtm/coding/Options.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/analyzer/Selector.hpp"
#include <functional>
#include <vector>
#include <cstdint>

namespace xtm::coding {

// Signature for the callback invoked on each 512x512 superblock
using SuperblockCallback = std::function<void(
    const terrain::IntGrid& sgrid, 
    uint32_t sx, uint32_t sy, 
    std::vector<partition::QuadtreeNode>& leaves,
    double quad_bits,
    const analyzer::PredictorSelector& selector,
    double partition_time_ms
)>;

void run_pipeline(
    const terrain::IntGrid& grid,
    const Options& options,
    const analyzer::PredictorSelector& global_selector,
    const SuperblockCallback& callback
);

} // namespace xtm::coding
