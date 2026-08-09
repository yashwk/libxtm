#include "xtm/coding/Pipeline.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>

namespace xtm::coding {

void run_pipeline(
    const terrain::IntGrid& grid,
    const Options& options,
    const analyzer::PredictorSelector& global_selector,
    const SuperblockCallback& callback
) {
    std::uint32_t superblock_size = 512;
    std::uint32_t num_superblocks_x = (grid.width + superblock_size - 1) / superblock_size;
    std::uint32_t num_superblocks_y = (grid.height + superblock_size - 1) / superblock_size;
    std::uint32_t num_superblocks_total = num_superblocks_x * num_superblocks_y;
    
    std::atomic<uint32_t> next_superblock_idx(0);
    
    auto worker = [&]() {
        terrain::IntGrid sgrid;
        std::vector<partition::QuadtreeNode> leaves;
        analyzer::PredictorSelector local_selector = global_selector;
        
        while (true) {
            uint32_t idx = next_superblock_idx.fetch_add(1);
            if (idx >= num_superblocks_total) break;
            
            std::uint32_t sy = (idx / num_superblocks_x) * superblock_size;
            std::uint32_t sx = (idx % num_superblocks_x) * superblock_size;
            
            sgrid.width = std::min(superblock_size, grid.width - sx);
            sgrid.height = std::min(superblock_size, grid.height - sy);
            sgrid.data.resize(sgrid.width * sgrid.height);
            sgrid.nodata_mask.resize(sgrid.width * sgrid.height, false);
            for (std::uint32_t y = 0; y < sgrid.height; ++y) {
                for (std::uint32_t x = 0; x < sgrid.width; ++x) {
                    uint32_t s_idx = y * sgrid.width + x;
                    uint32_t c_idx = (sy + y) * grid.width + (sx + x);
                    sgrid.data[s_idx] = grid.data[c_idx];
                    sgrid.nodata_mask[s_idx] = grid.nodata_mask.empty() ? false : grid.nodata_mask[c_idx];
                }
            }

            double quad_bits = 0.0;
            leaves.clear();
            
            auto t_quad_start = std::chrono::high_resolution_clock::now();
            if (options.disable_quadtree) {
                const terrain::IntGrid& sgrid_const = sgrid;
                auto blocks = partition::FixedGridPartitioner::partition(sgrid_const, 64);
                for (const auto& b : blocks) {
                    partition::QuadtreeNode leaf;
                    leaf.block = b;
                    leaf.selection = local_selector.select(b);
                    leaf.is_split = false;
                    leaves.push_back(std::move(leaf));
                }
            } else {
                partition::QuadtreePartitioner::partition(sgrid, 512, 64, local_selector, quad_bits, leaves);
            }
            auto t_quad_end = std::chrono::high_resolution_clock::now();
            double partition_time_ms = std::chrono::duration<double, std::milli>(t_quad_end - t_quad_start).count();
            
            callback(sgrid, sx, sy, leaves, quad_bits, local_selector, partition_time_ms);
        }
    };
    
    uint32_t nt = options.num_threads == 0 ? std::thread::hardware_concurrency() : options.num_threads;
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < nt; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }
}

} // namespace xtm::coding
