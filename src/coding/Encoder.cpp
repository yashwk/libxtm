#include "xtm/coding/Encoder.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/predictor/Predictors.hpp"
#include "xtm/analyzer/Statistics.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cmath>
#include <array>
#include <cstring>
#include <unordered_map>
#include "xtm/coding/Pipeline.hpp"

namespace xtm::coding {

EncodeResult XtmEncoder::encode(const terrain::IntGrid& grid,
                                container::XtmWriter& writer,
                                const PipelineContext& ctx) {
    using namespace std::chrono;
    EncodeResult result;
    std::mutex stats_mutex;
    
    predictor::PredictorBank bank;
    std::vector<const predictor::Predictor*> predictors_list = bank.ordered();
    analyzer::PredictorSelector global_selector(predictors_list, ctx);
    
    coding::for_each_superblock(grid, ctx, global_selector,
        [&](const terrain::IntGrid& /*sgrid*/, uint32_t sx, uint32_t sy, uint32_t s_idx,
            std::vector<partition::QuadtreeNode>& leaves, double /*quad_bits*/,
            const analyzer::PredictorSelector& /*selector*/, double partition_time_ms) {
        double local_time_quadtree = partition_time_ms;
        double local_time_entropy = 0.0;
        double local_time_io = 0.0;
        std::uint32_t local_total_blocks = leaves.size();
        std::array<PredictorStats, 32> local_predictor_stats{};
        coding::EncodingContext ctx_data;
        coding::BitWriter bw;
        std::vector<container::XtmWriter::PendingBlock> local_blocks;
        
        uint32_t leaf_idx = 0;
        for (auto& leaf : leaves) {
            const std::vector<int32_t>& data = leaf.selection.best_residuals;
            
            auto t_ent_start = high_resolution_clock::now();
            bw.reset();
            
            if (ctx.pipeline_type == analyzer::PipelineType::Predictor) {
                uint32_t predictor_idx = 0;
                for (uint32_t i = 0; i < predictors_list.size(); ++i) {
                    if (predictors_list[i] == leaf.selection.best_predictor) {
                        predictor_idx = i;
                        break;
                    }
                }
                
                auto& stats = local_predictor_stats[predictor_idx];
                stats.count++;
                
                // One byte: 5-bit primary predictor id + 3-bit residual
                // predictor id (0 = none). The second-order stage is signaled
                // by the residual id.
                uint32_t byte = (static_cast<uint32_t>(leaf.selection.residual_predictor_id) << 5) | predictor_idx;
                bw.write_bits(byte, 8);
                
                if (ctx.has_precision) {
                    // 0xFF = no precision plane, 0xFE = raw passthrough (no
                    // prediction — chosen when the digit values are already
                    // incompressible), otherwise the predictor id.
                    uint32_t prec_pid_raw = 0xFF;
                    if (leaf.selection.best_prec_raw) {
                        prec_pid_raw = 0xFE;
                    } else if (leaf.selection.best_prec_predictor) {
                        prec_pid_raw = static_cast<uint32_t>(leaf.selection.best_prec_predictor->id());
                    }
                    bw.write_bits(prec_pid_raw, 8);
                }
                
                bw.write_bits(leaf.selection.best_parameters.size(), 8);
                for (int32_t p : leaf.selection.best_parameters) {
                    uint32_t p_bits;
                    std::memcpy(&p_bits, &p, sizeof(int32_t));
                    bw.write_bits(p_bits, 32);
                }
                if (leaf.selection.residual_predictor_id != analyzer::ResidualPredictorId::None) {
                    bw.write_bits(leaf.selection.best_residual_parameters.size(), 8);
                    for (int32_t p : leaf.selection.best_residual_parameters) {
                        uint32_t p_bits;
                        std::memcpy(&p_bits, &p, sizeof(int32_t));
                        bw.write_bits(p_bits, 32);
                    }
                }
            }
            
            bool block_has_nodata = false;
            for (uint32_t y = 0; y < leaf.block.height; ++y) {
                for (uint32_t x = 0; x < leaf.block.width; ++x) {
                    if (leaf.block.grid->nodata_mask[(leaf.block.y_offset + y) * leaf.block.grid->width + (leaf.block.x_offset + x)]) {
                        block_has_nodata = true;
                        break;
                    }
                }
                if (block_has_nodata) break;
            }
            
            bw.write_bits(block_has_nodata ? 1 : 0, 1);
            if (block_has_nodata) {
                bool current_val = false;
                uint32_t run = 0;
                for (uint32_t y = 0; y < leaf.block.height; ++y) {
                    for (uint32_t x = 0; x < leaf.block.width; ++x) {
                        bool v = leaf.block.grid->nodata_mask[(leaf.block.y_offset + y) * leaf.block.grid->width + (leaf.block.x_offset + x)];
                        if (v == current_val) {
                            run++;
                        } else {
                            while (run >= 255) {
                                bw.write_bits(255, 8);
                                bw.write_bits(0, 8);
                                run -= 255;
                            }
                            bw.write_bits(run, 8);
                            current_val = v;
                            run = 1;
                        }
                    }
                }
                while (run >= 255) {
                    bw.write_bits(255, 8);
                    bw.write_bits(0, 8);
                    run -= 255;
                }
                bw.write_bits(run, 8);
            }
            
            coding::ArithmeticEncoder ac(bw);
            ctx_data.reset();
            coding::encode_stream(data, leaf.block.width, leaf.block.height, ctx, ac, ctx_data);
            
            ac.flush();
            bw.flush();
            auto t_ent_end = high_resolution_clock::now();
            local_time_entropy += duration<double, std::milli>(t_ent_end - t_ent_start).count();
            
            auto t_io_start = high_resolution_clock::now();
            uint64_t seq_id = (static_cast<uint64_t>(s_idx) << 32) | leaf_idx;
            container::XtmWriter::PendingBlock pb;
            pb.x = sx + leaf.block.x_offset;
            pb.y = sy + leaf.block.y_offset;
            pb.width = leaf.block.width;
            pb.height = leaf.block.height;
            pb.bitstream = bw.get_buffer();
            pb.sequence_id = seq_id;
            local_blocks.push_back(std::move(pb));
            leaf_idx++;
            auto t_io_end = high_resolution_clock::now();
            local_time_io += duration<double, std::milli>(t_io_end - t_io_start).count();
        }
        
        auto t_io_sb_start = high_resolution_clock::now();
        writer.write_superblock(s_idx, std::move(local_blocks));
        auto t_io_sb_end = high_resolution_clock::now();
        local_time_io += duration<double, std::milli>(t_io_sb_end - t_io_sb_start).count();
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        result.time_quadtree += local_time_quadtree;
        result.time_entropy += local_time_entropy;
        result.time_io += local_time_io;
        result.total_blocks += local_total_blocks;
        
        for (size_t i = 0; i < local_predictor_stats.size(); ++i) {
            if (local_predictor_stats[i].count == 0) continue;
            auto& ds = result.predictor_stats[static_cast<uint32_t>(i)];
            ds.count += local_predictor_stats[i].count;
        }
    });
    
    return result;
}

} // namespace xtm::coding
