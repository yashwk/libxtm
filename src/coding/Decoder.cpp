#include "xtm/coding/Decoder.hpp"
#include "xtm/partition/Block.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/predictor/Predictors.hpp"
#include "xtm/predictor/SplitPrecisionWrapper.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace xtm::coding {

DecodeResult XtmDecoder::decode(container::XtmReader& reader,
                                terrain::IntGrid& roi_grid,
                                int rx, int ry, int rw, int rh,
                                uint32_t num_threads) {
    DecodeResult result;
    const auto& header = reader.get_header();
    const auto& index = reader.get_index();
    
    analyzer::PipelineType pipeline_type = (header.pipeline_id == container::XtmHeader::PIPELINE_WAVELET) ? analyzer::PipelineType::Wavelet : analyzer::PipelineType::Predictor;
    ContextModel context_model = (header.context_model == 1) ? ContextModel::Extended : ContextModel::Simple;
    bool has_precision = (header.scale < 1.0f);
    
    predictor::PredictorBank bank;
    
    std::uint32_t superblock_size = 512;
    std::uint32_t start_sx = (rx / superblock_size) * superblock_size;
    std::uint32_t start_sy = (ry / superblock_size) * superblock_size;
    std::uint32_t end_sx = ((rx + rw + superblock_size - 1) / superblock_size) * superblock_size;
    std::uint32_t end_sy = ((ry + rh + superblock_size - 1) / superblock_size) * superblock_size;
    
    std::uint32_t num_superblocks_x = (end_sx - start_sx + superblock_size - 1) / superblock_size;
    std::uint32_t num_superblocks_y = (end_sy - start_sy + superblock_size - 1) / superblock_size;
    std::uint32_t num_superblocks_total = num_superblocks_x * num_superblocks_y;
    
    std::atomic<uint32_t> next_superblock_idx(0);
    std::atomic<uint32_t> blocks_decoded(0);
    
    std::atomic<bool> decode_failed{false};
    std::string decode_error_msg;
    std::mutex decode_error_mutex;
    
    auto worker = [&]() {
        try {
            uint32_t local_blocks_decoded = 0;
            coding::EncodingContext ctx_data;
            
            terrain::IntGrid sgrid;
            std::vector<container::BlockIndexEntry> sblocks;
            std::vector<uint8_t> block_buffer;
            predictor::PredictionResult decoded_res;
            
            while (true) {
                if (decode_failed) break;
                uint32_t idx = next_superblock_idx.fetch_add(1);
                if (idx >= num_superblocks_total) break;
                
                std::uint32_t sy = start_sy + (idx / num_superblocks_x) * superblock_size;
                std::uint32_t sx = start_sx + (idx % num_superblocks_x) * superblock_size;
                
                if (sy >= header.grid_height || sx >= header.grid_width) continue;
                
                sgrid.width = std::min(superblock_size, header.grid_width - sx);
                sgrid.height = std::min(superblock_size, header.grid_height - sy);
                sgrid.data.resize(sgrid.width * sgrid.height, 0);
                sgrid.nodata_mask.resize(sgrid.width * sgrid.height, false);
                
                sblocks.clear();
                for (const auto& entry : index) {
                    if (entry.block_x >= sx && entry.block_x < sx + sgrid.width &&
                        entry.block_y >= sy && entry.block_y < sy + sgrid.height) {
                        sblocks.push_back(entry);
                    }
                }
                
                // Removed std::sort to preserve the Z-order naturally present in the index,
                // which is required for correct decoding of context-dependent predictors.
                
                for (const auto& entry : sblocks) {
                    local_blocks_decoded++;
                    reader.read_block(entry, block_buffer);
                    coding::BitReader br(block_buffer);
                    
                    const predictor::Predictor* predictor = nullptr;
                    const predictor::Predictor* prec_predictor = nullptr;
                    bool use_second_order = false;
                    
                    decoded_res.residuals.clear();
                    decoded_res.parameters.clear();
                    
                    if (pipeline_type == analyzer::PipelineType::Predictor) {
                        uint32_t predictor_id_raw = br.read_bits(8);
                        use_second_order = (predictor_id_raw & 0x80) != 0;
                        uint32_t predictor_id = predictor_id_raw & 0x7F;
                        
                        predictor = bank.by_id(static_cast<predictor::PredictorId>(predictor_id));
                        if (!predictor) {
                            throw std::runtime_error("Corrupt XTM: unknown predictor id " + std::to_string(predictor_id));
                        }
                        
                        if (has_precision) {
                            uint32_t prec_pid_raw = br.read_bits(8);
                            if (prec_pid_raw != 0xFF) {
                                prec_predictor = bank.by_id(static_cast<predictor::PredictorId>(prec_pid_raw));
                            }
                        }
                        
                        uint32_t num_params = br.read_bits(8);
                        for (uint32_t i = 0; i < num_params; ++i) {
                            uint32_t p_bits = br.read_bits(32);
                            int32_t p;
                            std::memcpy(&p, &p_bits, sizeof(int32_t));
                            decoded_res.parameters.push_back(p);
                        }
                    }
                    
                    bool block_has_nodata = br.read_bits(1) != 0;
                    if (block_has_nodata) {
                        bool current_val = false;
                        uint32_t pixels_read = 0;
                        uint32_t total_pixels = entry.block_width * entry.block_height;
                        
                        while (pixels_read < total_pixels) {
                            uint32_t run = 0;
                            while (true) {
                                uint32_t r = br.read_bits(8);
                                run += r;
                                if (r == 255) {
                                    br.read_bits(8); // consume the 0
                                } else {
                                    break;
                                }
                            }
                            
                            for (uint32_t i = 0; i < run && pixels_read < total_pixels; ++i) {
                                uint32_t px = pixels_read % entry.block_width;
                                uint32_t py = pixels_read / entry.block_width;
                                uint32_t sgrid_x = entry.block_x - sx;
                                uint32_t sgrid_y = entry.block_y - sy;
                                sgrid.nodata_mask[(sgrid_y + py) * sgrid.width + (sgrid_x + px)] = current_val;
                                pixels_read++;
                            }
                            
                            current_val = !current_val;
                        }
                    }
                    
                    int32_t prec_multiplier = has_precision ? static_cast<int32_t>(std::round(1.0 / header.scale)) : 1;
                    std::unique_ptr<predictor::SplitPrecisionWrapper> sp_wrapper;
                    if (pipeline_type == analyzer::PipelineType::Predictor && has_precision && prec_predictor != nullptr) {
                        sp_wrapper = std::make_unique<predictor::SplitPrecisionWrapper>(prec_multiplier, predictor, prec_predictor);
                        predictor = sp_wrapper.get();
                    }
                    
                    uint32_t max_levels = (pipeline_type == analyzer::PipelineType::Wavelet) ? coding::max_wavelet_levels(entry.block_width, entry.block_height) : 0;
                    
                    coding::ArithmeticDecoder ad(br);
                    ctx_data.reset();
                    
                    uint32_t total_symbols = entry.block_width * entry.block_height;
                    bool is_split_precision = (pipeline_type == analyzer::PipelineType::Predictor) && has_precision && (prec_predictor != nullptr);
                    uint32_t total_stream_len = is_split_precision ? (total_symbols * 2) : total_symbols;
                    decoded_res.residuals.assign(total_stream_len, 0);
                    
                    coding::decode_stream(decoded_res.residuals, entry.block_width, entry.block_height, context_model, has_precision, ad, ctx_data);
                    
                    auto apply_second_order_reversal = [&]() {
                        if (use_second_order) {
                            for (uint32_t y = 0; y < entry.block_height; ++y) {
                                for (uint32_t x = 0; x < entry.block_width; ++x) {
                                    int32_t r = decoded_res.residuals[y * entry.block_width + x];
                                    int32_t w_val = (x > 0) ? decoded_res.residuals[y * entry.block_width + x - 1] : 0;
                                    int32_t n_val = (y > 0) ? decoded_res.residuals[(y - 1) * entry.block_width + x] : 0;
                                    
                                    int32_t p = 0;
                                    if (x > 0 && y > 0) p = w_val / 2 + n_val / 2;
                                    else if (x > 0) p = w_val;
                                    else if (y > 0) p = n_val;
                                    
                                    decoded_res.residuals[y * entry.block_width + x] = r + p;
                                }
                            }
                        }
                    };
                    
                    if (br.excess_bits() > 512) {
                        throw std::runtime_error("Corrupt XTM: block bitstream underflow "
                                                 + std::to_string(br.excess_bits()) + " bits past end-of-stream");
                    }
                    
                    partition::MutableBlockView mbv;
                    mbv.grid = &sgrid;
                    mbv.x_offset = entry.block_x - sx;
                    mbv.y_offset = entry.block_y - sy;
                    mbv.width = entry.block_width;
                    mbv.height = entry.block_height;
                    
                    if (pipeline_type == analyzer::PipelineType::Wavelet) {
                        transform::CDF53Transform::inverse_2d(decoded_res.residuals, entry.block_width, entry.block_height, max_levels);
                        for (uint32_t y = 0; y < entry.block_height; ++y) {
                            for (uint32_t x = 0; x < entry.block_width; ++x) {
                                mbv.set(x, y, decoded_res.residuals[y * entry.block_width + x]);
                            }
                        }
                    } else {
                        apply_second_order_reversal();
                        predictor->decode(decoded_res, mbv);
                    }
                }
                
                for (std::uint32_t y = 0; y < sgrid.height; ++y) {
                    for (std::uint32_t x = 0; x < sgrid.width; ++x) {
                        std::uint32_t global_x = sx + x;
                        std::uint32_t global_y = sy + y;
                        if (global_x >= (uint32_t)rx && global_x < (uint32_t)(rx + rw) &&
                            global_y >= (uint32_t)ry && global_y < (uint32_t)(ry + rh)) {
                            roi_grid.data[(global_y - ry) * rw + (global_x - rx)] = sgrid.data[y * sgrid.width + x];
                            roi_grid.nodata_mask[(global_y - ry) * rw + (global_x - rx)] = sgrid.nodata_mask[y * sgrid.width + x];
                        }
                    }
                }
            }
            
            blocks_decoded.fetch_add(local_blocks_decoded);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(decode_error_mutex);
            if (!decode_failed) {
                decode_error_msg = e.what();
                decode_failed = true;
            }
        }
    };
    
    uint32_t threads_to_use = num_threads == 0 ? std::thread::hardware_concurrency() : num_threads;
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < threads_to_use; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }
    
    if (decode_failed) {
        throw std::runtime_error(decode_error_msg);
    }
    
    result.blocks_decoded = blocks_decoded;
    return result;
}

} // namespace xtm::coding
