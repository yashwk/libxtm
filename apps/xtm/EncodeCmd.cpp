#include "EncodeCmd.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/terrain/Quantization.hpp"
#include "xtm/analyzer/Selector.hpp"
#include "xtm/analyzer/Statistics.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/predictor/Predictors.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <map>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>

namespace xtm::cli {

int run_encode(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: xtm encode <input.tif> -o <output.xtm> [--scale <value>]\n";
        return 1;
    }
    
    std::string input_file = "";
    std::string output_file = "";
    double scale = 1.0;
    bool disable_quadtree = false;
    coding::ContextModel model = coding::ContextModel::Simple;
    analyzer::PipelineOrder pipeline_order = analyzer::PipelineOrder::PredictorWavelet;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            scale = std::stod(argv[++i]);
        } else if (arg == "--context" && i + 1 < argc) {
            std::string ctype = argv[++i];
            if (ctype == "simple") model = coding::ContextModel::Simple;
            else if (ctype == "extended") model = coding::ContextModel::Extended;
            else {
                std::cerr << "Unknown context model: " << ctype << "\n";
                return 1;
            }
        } else if (arg == "--pipeline" && i + 1 < argc) {
            std::string pline = argv[++i];
            if (pline == "predictor-wavelet") pipeline_order = analyzer::PipelineOrder::PredictorWavelet;
            else if (pline == "wavelet-predictor") pipeline_order = analyzer::PipelineOrder::WaveletPredictor;
            else {
                std::cerr << "Unknown pipeline: " << pline << "\n";
                return 1;
            }
        } else if (arg == "--disable-quadtree") {
            disable_quadtree = true;
        } else if (input_file.empty() && arg[0] != '-') {
            input_file = arg;
        }
    }
    
    if (input_file.empty()) {
        std::cerr << "Error: Input file is required.\n";
        return 1;
    }
    
    if (output_file.empty()) {
        std::cerr << "Error: Output file (-o) is required.\n";
        return 1;
    }
    
    using namespace std::chrono;
    
    try {
        auto t_start = high_resolution_clock::now();
        
        std::cout << "Loading dataset: " << input_file << "...\n";
        auto buffer = io::read_gdal(input_file);
        
        std::cout << "Encoding terrain (scale=" << scale << ") to " << output_file << "...\n";
        if (disable_quadtree) {
            std::cout << "Quadtree disabled (using fixed 64x64 blocks).\n";
        }
        
        auto t_quant_start = high_resolution_clock::now();
        auto cgrid = terrain::quantize(buffer.view(), scale);
        auto t_quant_end = high_resolution_clock::now();
        double time_quant = duration<double, std::milli>(t_quant_end - t_quant_start).count();
        
        predictor::PredictorBank bank;
        std::vector<const predictor::Predictor*> predictors_list = bank.ordered();
        
        container::XtmHeader header;
        header.grid_width = cgrid.width;
        header.grid_height = cgrid.height;
        header.res_x = scale;
        header.res_y = scale;
        header.context_model = (model == coding::ContextModel::Extended) ? 1 : 0;
        if (buffer.nodata_value.has_value()) {
            header.flags |= container::XtmHeader::FLAG_HAS_NODATA;
            header.nodata_value = *buffer.nodata_value;
        }
        if (pipeline_order == analyzer::PipelineOrder::WaveletPredictor) {
            header.flags |= container::XtmHeader::FLAG_WAVELET_FIRST;
        }
        
        const auto& gt = buffer.view().transform;
        header.min_x = gt.origin_x;
        header.max_x = gt.origin_x + cgrid.width * gt.pixel_width;
        if (gt.pixel_height < 0) {
            header.max_y = gt.origin_y;
            header.min_y = gt.origin_y + cgrid.height * gt.pixel_height;
        } else {
            header.min_y = gt.origin_y;
            header.max_y = gt.origin_y + cgrid.height * gt.pixel_height;
        }
        
        container::XtmWriter writer(output_file, header);
        
        analyzer::PredictorSelector selector(predictors_list, 10.0, pipeline_order);
        
        std::uint32_t superblock_size = 512;
        std::uint32_t total_blocks = 0;
        
        struct PredictorStats {
            uint32_t count = 0;
            double sum_entropy = 0;
            double sum_mean_abs = 0;
            double sum_variance = 0;
        };
        std::map<uint32_t, PredictorStats> predictor_stats;
        uint32_t wavelet_blocks = 0;
        
        double time_quadtree = 0.0;
        double time_entropy = 0.0;
        double time_io = 0.0;
        
        std::uint32_t num_superblocks_x = (cgrid.width + superblock_size - 1) / superblock_size;
        std::uint32_t num_superblocks_y = (cgrid.height + superblock_size - 1) / superblock_size;
        std::uint32_t num_superblocks_total = num_superblocks_x * num_superblocks_y;
        
        std::atomic<uint32_t> next_superblock_idx(0);
        std::mutex stats_mutex;
        
        auto worker = [&]() {
            double local_time_quadtree = 0.0;
            double local_time_entropy = 0.0;
            double local_time_io = 0.0;
            
            std::uint32_t local_total_blocks = 0;
            std::uint32_t local_wavelet_blocks = 0;
            std::map<uint32_t, PredictorStats> local_predictor_stats;
            
            while (true) {
                uint32_t idx = next_superblock_idx.fetch_add(1);
                if (idx >= num_superblocks_total) break;
                
                std::uint32_t sy = (idx / num_superblocks_x) * superblock_size;
                std::uint32_t sx = (idx % num_superblocks_x) * superblock_size;
                
                // Extract Superblock
                terrain::IntGrid sgrid;
                sgrid.width = std::min(superblock_size, cgrid.width - sx);
                sgrid.height = std::min(superblock_size, cgrid.height - sy);
                sgrid.data.resize(sgrid.width * sgrid.height);
                sgrid.nodata_mask.resize(sgrid.width * sgrid.height, false);
                for (std::uint32_t y = 0; y < sgrid.height; ++y) {
                    for (std::uint32_t x = 0; x < sgrid.width; ++x) {
                        uint32_t s_idx = y * sgrid.width + x;
                        uint32_t c_idx = (sy + y) * cgrid.width + (sx + x);
                        sgrid.data[s_idx] = cgrid.data[c_idx];
                        sgrid.nodata_mask[s_idx] = cgrid.nodata_mask[c_idx];
                    }
                }
                
                auto t_quad_start = high_resolution_clock::now();
                double dummy_bits = 0.0;
                
                std::vector<partition::QuadtreeNode> leaves;
                if (disable_quadtree) {
                    const terrain::IntGrid& sgrid_const = sgrid;
                    auto blocks = partition::FixedGridPartitioner::partition(sgrid_const, 64);
                    for (const auto& b : blocks) {
                        partition::QuadtreeNode leaf;
                        leaf.block = b;
                        leaf.selection = selector.select(b);
                        leaf.is_split = false;
                        leaves.push_back(std::move(leaf));
                    }
                } else {
                    leaves = partition::QuadtreePartitioner::partition(sgrid, 512, 64, selector, dummy_bits);
                }
                auto t_quad_end = high_resolution_clock::now();
                local_time_quadtree += duration<double, std::milli>(t_quad_end - t_quad_start).count();
                
                local_total_blocks += leaves.size();
                
                for (auto& leaf : leaves) {
                    bool use_wavelet = leaf.selection.use_wavelet;
                    uint32_t max_levels = leaf.selection.wavelet_levels;
                    std::vector<int32_t> data = leaf.selection.best_encoded.residuals;
                    
                    auto t_ent_start = high_resolution_clock::now();
                    coding::BitWriter bw;
                    
                    uint32_t predictor_idx = 0;
                    for (uint32_t i = 0; i < predictors_list.size(); ++i) {
                        if (predictors_list[i] == leaf.selection.best_predictor) {
                            predictor_idx = i;
                            break;
                        }
                    }
                    
                    double entropy = analyzer::calculate_entropy(data);
                    double sum_abs = 0;
                    double sum_val = 0;
                    for (int32_t v : data) {
                        sum_abs += std::abs(v);
                        sum_val += v;
                    }
                    double mean_abs = data.empty() ? 0 : sum_abs / data.size();
                    double mean = data.empty() ? 0 : sum_val / data.size();
                    double variance = 0;
                    for (int32_t v : data) {
                        variance += (v - mean) * (v - mean);
                    }
                    if (!data.empty()) variance /= data.size();
                    
                    auto& stats = local_predictor_stats[predictor_idx];
                    stats.count++;
                    stats.sum_entropy += entropy;
                    stats.sum_mean_abs += mean_abs;
                    stats.sum_variance += variance;
                    
                    if (use_wavelet) local_wavelet_blocks++;
                    
                    bw.write_bits(predictor_idx, 8);
                    bw.write_bits(use_wavelet ? 1 : 0, 1);
                    
                    bw.write_bits(leaf.selection.best_encoded.parameters.size(), 8);
                    for (int32_t p : leaf.selection.best_encoded.parameters) {
                        uint32_t p_bits;
                        std::memcpy(&p_bits, &p, sizeof(int32_t));
                        bw.write_bits(p_bits, 32);
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
                    std::unordered_map<coding::Context, coding::FrequencyTable> context_tables;
                    coding::FrequencyTable run_table(256);
                    
                    auto symbols = coding::generate_symbols(data, leaf.block.width, leaf.block.height, max_levels, model);
                    coding::FrequencyTable uniform_bit(2);
                    
                    for (const auto& sym : symbols) {
                        if (context_tables.find(sym.context) == context_tables.end()) {
                            context_tables.emplace(sym.context, coding::FrequencyTable(33));
                        }
                        auto& freqs = context_tables.at(sym.context);
                        
                        ac.encode(freqs, sym.magnitude_class);
                        freqs.increment(sym.magnitude_class);
                        
                        if (sym.magnitude_class == 0) {
                            ac.encode(run_table, sym.run_length - 1);
                            run_table.increment(sym.run_length - 1);
                        } else if (sym.magnitude_class > 1) {
                            for (int i = sym.magnitude_class - 2; i >= 0; --i) {
                                uint32_t bit = (sym.remainder >> i) & 1;
                                ac.encode(uniform_bit, bit);
                            }
                        }
                    }
                    
                    ac.flush();
                    bw.flush();
                    auto t_ent_end = high_resolution_clock::now();
                    local_time_entropy += duration<double, std::milli>(t_ent_end - t_ent_start).count();
                    
                    auto t_io_start = high_resolution_clock::now();
                    writer.write_block(sx + leaf.block.x_offset, sy + leaf.block.y_offset, leaf.block.width, leaf.block.height, bw.get_buffer());
                    auto t_io_end = high_resolution_clock::now();
                    local_time_io += duration<double, std::milli>(t_io_end - t_io_start).count();
                }
            }
            
            std::lock_guard<std::mutex> lock(stats_mutex);
            time_quadtree += local_time_quadtree;
            time_entropy += local_time_entropy;
            time_io += local_time_io;
            total_blocks += local_total_blocks;
            wavelet_blocks += local_wavelet_blocks;
            
            for (const auto& kv : local_predictor_stats) {
                auto& ds = predictor_stats[kv.first];
                ds.count += kv.second.count;
                ds.sum_entropy += kv.second.sum_entropy;
                ds.sum_mean_abs += kv.second.sum_mean_abs;
                ds.sum_variance += kv.second.sum_variance;
            }
        };
        
        uint32_t num_threads = std::thread::hardware_concurrency();
        std::vector<std::thread> threads;
        for (uint32_t i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker);
        }
        for (auto& t : threads) {
            t.join();
        }
        
        std::cout << "Partitioned into " << total_blocks << " independent blocks across superblocks.\n";
        std::cout << "Wavelet Transform applied to " << wavelet_blocks << " blocks (" 
                  << (double)wavelet_blocks / total_blocks * 100.0 << "%).\n";
        
        std::cout << "\n=== Predictor Statistics ===\n";
        for (const auto& kv : predictor_stats) {
            uint32_t count = kv.second.count;
            if (count == 0) continue;
            
            std::string name = "Unknown";
            if (kv.first < predictors_list.size()) {
                name = predictors_list[kv.first]->name();
            }
            
            std::cout << "- " << name << ":\n"
                      << "    Usage:              " << std::fixed << std::setprecision(2) << (double)count / total_blocks * 100.0 << "%\n"
                      << "    Average Entropy:    " << kv.second.sum_entropy / count << "\n"
                      << "    Mean Absolute Res:  " << kv.second.sum_mean_abs / count << "\n"
                      << "    Residual Variance:  " << kv.second.sum_variance / count << "\n";
            // Machine-readable summary consumed by utils/benchmark_suite.py
            std::cout << "Predictor " << kv.first << ": " << count << " blocks ("
                      << (double)count / total_blocks * 100.0 << "%)\n";
        }
        std::cout << "============================\n";
        
        writer.finalize();
        
        auto t_end = high_resolution_clock::now();
        double time_total = duration<double, std::milli>(t_end - t_start).count();
        
        std::cout << "\n=== Pipeline Profiling ===\n";
        std::cout << "  Quantization:         " << time_quant << " ms\n";
        std::cout << "  Quadtree/Prediction:  " << time_quadtree << " ms\n";
        std::cout << "  Entropy Coding:       " << time_entropy << " ms\n";
        std::cout << "  Container IO:         " << time_io << " ms\n";
        std::cout << "  Total Execution:      " << time_total << " ms\n";
        std::cout << "==========================\n";
        
        std::cout << "Successfully encoded to " << output_file << "!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

} // namespace xtm::cli
