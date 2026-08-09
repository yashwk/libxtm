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
#include <cstring>
#include <unordered_map>
#include "xtm/coding/Pipeline.hpp"

namespace xtm::coding {

EncodeResult XtmEncoder::encode(const terrain::IntGrid& grid,
                                container::XtmWriter& writer,
                                const Options& options) {
    using namespace std::chrono;
    EncodeResult result;
    
    predictor::PredictorBank bank;
    std::vector<const predictor::Predictor*> predictors_list = bank.ordered();
    bool has_precision = options.scale < 1.0f;
    int32_t precision_multiplier = has_precision ? static_cast<int32_t>(std::round(1.0 / options.scale)) : 1;
    
    std::mutex stats_mutex;
    std::uint32_t num_superblocks_x = (grid.width + 512 - 1) / 512;
    
    analyzer::PredictorSelector global_selector(predictors_list, options.pipeline_type, precision_multiplier, options.context_model);
    
    coding::run_pipeline(grid, options, global_selector, [&](const terrain::IntGrid& sgrid, uint32_t sx, uint32_t sy, std::vector<partition::QuadtreeNode>& leaves, double /*quad_bits*/, const analyzer::PredictorSelector& /*selector*/, double partition_time_ms) {
        (void)sgrid;
        double local_time_quadtree = partition_time_ms;
        double local_time_entropy = 0.0;
        double local_time_io = 0.0;
        std::uint32_t local_total_blocks = leaves.size();
        std::map<uint32_t, PredictorStats> local_predictor_stats;
        coding::EncodingContext ctx_data;
        coding::BitWriter bw;
        
        uint32_t s_idx = (sy / 512) * num_superblocks_x + (sx / 512);
        
        uint32_t leaf_idx = 0;
        for (auto& leaf : leaves) {
            std::vector<int32_t> data = leaf.selection.best_encoded.residuals;
            
            auto t_ent_start = high_resolution_clock::now();
            bw.reset();
            
            if (options.pipeline_type == analyzer::PipelineType::Predictor) {
                uint32_t predictor_idx = 0;
                for (uint32_t i = 0; i < predictors_list.size(); ++i) {
                    if (predictors_list[i] == leaf.selection.best_predictor) {
                        predictor_idx = i;
                        break;
                    }
                }
                
                auto& stats = local_predictor_stats[predictor_idx];
                stats.count++;
                
                uint32_t pid_raw = static_cast<uint32_t>(leaf.selection.best_predictor->id());
                if (leaf.selection.use_second_order) {
                    pid_raw |= 0x80;
                }
                bw.write_bits(pid_raw, 8);
                
                if (has_precision) {
                    uint32_t prec_pid_raw = leaf.selection.best_prec_predictor ? static_cast<uint32_t>(leaf.selection.best_prec_predictor->id()) : 0xFF;
                    bw.write_bits(prec_pid_raw, 8);
                }
                
                bw.write_bits(leaf.selection.best_encoded.parameters.size(), 8);
                for (int32_t p : leaf.selection.best_encoded.parameters) {
                    uint32_t p_bits;
                    std::memcpy(&p_bits, &p, sizeof(int32_t));
                    bw.write_bits(p_bits, 32);
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
            coding::encode_stream(data, leaf.block.width, leaf.block.height, options.context_model, has_precision, ac, ctx_data);
            
            ac.flush();
            bw.flush();
            auto t_ent_end = high_resolution_clock::now();
            local_time_entropy += duration<double, std::milli>(t_ent_end - t_ent_start).count();
            
            auto t_io_start = high_resolution_clock::now();
            uint64_t seq_id = (static_cast<uint64_t>(s_idx) << 32) | leaf_idx;
            writer.write_block(sx + leaf.block.x_offset, sy + leaf.block.y_offset, leaf.block.width, leaf.block.height, bw.get_buffer(), seq_id);
            leaf_idx++;
            auto t_io_end = high_resolution_clock::now();
            local_time_io += duration<double, std::milli>(t_io_end - t_io_start).count();
        }
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        result.time_quadtree += local_time_quadtree;
        result.time_entropy += local_time_entropy;
        result.time_io += local_time_io;
        result.total_blocks += local_total_blocks;
        
        for (const auto& kv : local_predictor_stats) {
            auto& ds = result.predictor_stats[kv.first];
            ds.count += kv.second.count;
        }
    });
    
    return result;
}

} // namespace xtm::coding
