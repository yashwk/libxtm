#pragma once
#include "xtm/Terrain.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/analyzer/Selector.hpp"
#include <functional>
#include <cstdint>
#include <vector>

namespace xtm::coding {

// Iterator to fetch the next superblock inside a worker thread
using SuperblockIterator = std::function<bool(
    uint32_t& sx, uint32_t& sy, 
    uint32_t& sgrid_w, uint32_t& sgrid_h, 
    uint32_t& s_idx
)>;

// Worker callback that runs in each thread
using WorkerCallback = std::function<void(const SuperblockIterator& next_sb)>;

void parallel_for_superblocks(
    uint32_t grid_width, uint32_t grid_height,
    uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh,
    uint32_t superblock_size,
    uint32_t num_threads,
    const WorkerCallback& worker_func
);

// Callback invoked once per partitioned superblock inside a worker thread
using SuperblockHandler = std::function<void(
    const terrain::IntGrid& sgrid,
    uint32_t sx, uint32_t sy, uint32_t s_idx,
    std::vector<partition::QuadtreeNode>& leaves,
    double quad_bits,
    const analyzer::PredictorSelector& selector,
    double partition_time_ms
)>;

// Extracts, partitions, and selects predictors for every 512x512 superblock of
// the grid in parallel, invoking handler once per superblock (superblocks
// outside the grid are skipped). Shared by the encoder and the analyzer.
void for_each_superblock(
    const terrain::IntGrid& grid,
    const PipelineContext& ctx,
    const analyzer::PredictorSelector& global_selector,
    const SuperblockHandler& handler
);

} // namespace xtm::coding
