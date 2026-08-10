#include "xtm/coding/Pipeline.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/analyzer/Selector.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <vector>
#include <stdexcept>

namespace xtm::coding {

void parallel_for_superblocks(
    uint32_t grid_width, uint32_t grid_height,
    uint32_t rx, uint32_t ry, uint32_t rw, uint32_t rh,
    uint32_t superblock_size,
    uint32_t num_threads,
    const WorkerCallback& worker_func
) {
    std::uint32_t start_sx = (rx / superblock_size) * superblock_size;
    std::uint32_t start_sy = (ry / superblock_size) * superblock_size;
    std::uint32_t end_sx = ((rx + rw + superblock_size - 1) / superblock_size) * superblock_size;
    std::uint32_t end_sy = ((ry + rh + superblock_size - 1) / superblock_size) * superblock_size;
    
    std::uint32_t num_superblocks_x = (end_sx - start_sx) / superblock_size;
    std::uint32_t num_superblocks_y = (end_sy - start_sy) / superblock_size;
    std::uint32_t num_superblocks_total = num_superblocks_x * num_superblocks_y;
    
    std::uint32_t grid_sb_x = (grid_width + superblock_size - 1) / superblock_size;
    
    std::atomic<uint32_t> next_idx(0);
    std::atomic<bool> failed{false};
    std::string error_msg;
    std::mutex error_mutex;
    
    SuperblockIterator next_sb = [&](uint32_t& sx, uint32_t& sy, uint32_t& sgrid_w, uint32_t& sgrid_h, uint32_t& s_idx) -> bool {
        while (true) {
            uint32_t idx = next_idx.fetch_add(1);
            if (idx >= num_superblocks_total || failed) return false;
            
            sy = start_sy + (idx / num_superblocks_x) * superblock_size;
            sx = start_sx + (idx % num_superblocks_x) * superblock_size;
            
            if (sy >= grid_height || sx >= grid_width) {
                sgrid_w = 0;
                sgrid_h = 0;
            } else {
                sgrid_w = std::min(superblock_size, grid_width - sx);
                sgrid_h = std::min(superblock_size, grid_height - sy);
            }
            
            s_idx = (sy / superblock_size) * grid_sb_x + (sx / superblock_size);
            return true;
        }
    };
    
    auto wrapper = [&]() {
        try {
            worker_func(next_sb);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(error_mutex);
            if (!failed) {
                error_msg = e.what();
                failed = true;
            }
        }
    };
    
    uint32_t nt = num_threads == 0 ? std::thread::hardware_concurrency() : num_threads;
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < nt; ++i) {
        threads.emplace_back(wrapper);
    }
    for (auto& t : threads) {
        t.join();
    }
    
    if (failed) {
        throw std::runtime_error(error_msg);
    }
}

void for_each_superblock(
    const terrain::IntGrid& grid,
    const PipelineContext& ctx,
    const analyzer::PredictorSelector& global_selector,
    const SuperblockHandler& handler
) {
    parallel_for_superblocks(
        grid.width, grid.height,
        0, 0, grid.width, grid.height,
        512, ctx.num_threads,
        [&](const SuperblockIterator& next_sb) {
            terrain::IntGrid sgrid;
            std::vector<partition::QuadtreeNode> leaves;
            analyzer::PredictorSelector local_selector = global_selector;
            
            uint32_t sx, sy, sgrid_w, sgrid_h, s_idx;
            while (next_sb(sx, sy, sgrid_w, sgrid_h, s_idx)) {
                if (sgrid_w == 0 || sgrid_h == 0) continue;
                
                sgrid.width = sgrid_w;
                sgrid.height = sgrid_h;
                sgrid.data.resize(sgrid.width * sgrid.height);
                sgrid.nodata_mask.resize(sgrid.width * sgrid.height, false);
                for (std::uint32_t y = 0; y < sgrid.height; ++y) {
                    for (std::uint32_t x = 0; x < sgrid.width; ++x) {
                        uint32_t sb_idx = y * sgrid.width + x;
                        uint32_t c_idx = (sy + y) * grid.width + (sx + x);
                        sgrid.data[sb_idx] = grid.data[c_idx];
                        sgrid.nodata_mask[sb_idx] = grid.nodata_mask.empty() ? false : grid.nodata_mask[c_idx];
                    }
                }
                
                double quad_bits = 0.0;
                leaves.clear();
                
                auto t_quad_start = std::chrono::high_resolution_clock::now();
                if (ctx.disable_quadtree) {
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
                
                handler(sgrid, sx, sy, s_idx, leaves, quad_bits, local_selector, partition_time_ms);
            }
        });
}

} // namespace xtm::coding
