#include "xtm/coding/Decoder.hpp"
#include "xtm/partition/Block.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/predictor/Predictors.hpp"
#include "xtm/coding/Pipeline.hpp"
#include "xtm/analyzer/Selector.hpp"

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
    coding::PipelineContext ctx(header.precision, context_model, pipeline_type);
    
    predictor::PredictorBank bank;
    
    std::atomic<uint32_t> blocks_decoded(0);
    
    coding::parallel_for_superblocks(
        header.grid_width, header.grid_height,
        rx, ry, rw, rh, 512, num_threads,
        [&](const coding::SuperblockIterator& next_sb) {
            uint32_t local_blocks_decoded = 0;
            coding::EncodingContext ctx_data;
            
            terrain::IntGrid sgrid;
            std::vector<container::BlockIndexEntry> sblocks;
            std::vector<uint8_t> block_buffer;
            std::vector<int32_t> decoded_residuals;
            std::vector<int32_t> decoded_parameters;
            std::vector<int32_t> decoded_resid_parameters;
            // Scratch buffers reused across blocks (capacity retained, no
            // per-block reallocation of the multi-MB split-precision grids).
            terrain::IntGrid rgrid;
            terrain::IntGrid m_grid;
            terrain::IntGrid p_grid;
            std::vector<int32_t> m_res;
            std::vector<int32_t> p_res;
            sgrid.data.reserve(512 * 512);
            sgrid.nodata_mask.reserve(512 * 512);
            block_buffer.reserve(1 << 20);
            decoded_residuals.reserve(512 * 512);
            decoded_parameters.reserve(16);
            
            uint32_t sx, sy, sgrid_w, sgrid_h, s_idx;
            while (next_sb(sx, sy, sgrid_w, sgrid_h, s_idx)) {
                sgrid.width = sgrid_w;
                sgrid.height = sgrid_h;
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
                    const predictor::Predictor* resid_predictor = nullptr;
                    analyzer::ResidualPredictorId resid_pid = analyzer::ResidualPredictorId::None;
                    bool prec_identity = false;
                    
                    decoded_residuals.clear();
                    decoded_parameters.clear();
                    decoded_resid_parameters.clear();
                    
                    if (ctx.pipeline_type == analyzer::PipelineType::Predictor) {
                        uint32_t pid_byte = br.read_bits(8);
                        uint32_t predictor_id = pid_byte & 0x1F;
                        uint32_t resid_raw = pid_byte >> 5;
                        if (resid_raw > 6) {
                            throw std::runtime_error("Corrupt XTM: unknown residual predictor id " + std::to_string(resid_raw));
                        }
                        resid_pid = static_cast<analyzer::ResidualPredictorId>(resid_raw);
                        if (resid_pid == analyzer::ResidualPredictorId::Left) resid_predictor = bank.by_id(predictor::PredictorId::Left);
                        else if (resid_pid == analyzer::ResidualPredictorId::Gradient) resid_predictor = bank.by_id(predictor::PredictorId::Gradient);
                        else if (resid_pid == analyzer::ResidualPredictorId::Gap) resid_predictor = bank.by_id(predictor::PredictorId::Gap);
                        else if (resid_pid == analyzer::ResidualPredictorId::LeastSquares) resid_predictor = bank.by_id(predictor::PredictorId::LeastSquares);
                        
                        predictor = bank.by_id(static_cast<predictor::PredictorId>(predictor_id));
                        if (!predictor) {
                            throw std::runtime_error("Corrupt XTM: unknown predictor id " + std::to_string(predictor_id));
                        }
                        
                        if (ctx.has_precision) {
                            uint32_t prec_pid_raw = br.read_bits(8);
                            if (prec_pid_raw == 0xFE) {
                                prec_identity = true; // raw passthrough: decoded values are the values
                            } else if (prec_pid_raw != 0xFF) {
                                prec_predictor = bank.by_id(static_cast<predictor::PredictorId>(prec_pid_raw));
                            }
                        }
                        
                        uint32_t num_params = br.read_bits(8);
                        for (uint32_t i = 0; i < num_params; ++i) {
                            uint32_t p_bits = br.read_bits(32);
                            int32_t p;
                            std::memcpy(&p, &p_bits, sizeof(int32_t));
                            decoded_parameters.push_back(p);
                        }
                        if (resid_pid != analyzer::ResidualPredictorId::None) {
                            uint32_t num_resid_params = br.read_bits(8);
                            for (uint32_t i = 0; i < num_resid_params; ++i) {
                                uint32_t p_bits = br.read_bits(32);
                                int32_t p;
                                std::memcpy(&p, &p_bits, sizeof(int32_t));
                                decoded_resid_parameters.push_back(p);
                            }
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
                    

                    
                    uint32_t max_levels = (ctx.pipeline_type == analyzer::PipelineType::Wavelet) ? coding::max_wavelet_levels(entry.block_width, entry.block_height) : 0;
                    
                    coding::ArithmeticDecoder ad(br);
                    ctx_data.reset();
                    
                    uint32_t total_symbols = entry.block_width * entry.block_height;
                    bool is_split_precision = (ctx.pipeline_type == analyzer::PipelineType::Predictor) && ctx.has_precision && (prec_predictor != nullptr || prec_identity);
                    uint32_t total_stream_len = is_split_precision ? (total_symbols * 2) : total_symbols;
                    decoded_residuals.assign(total_stream_len, 0);
                    
                    coding::decode_stream(decoded_residuals, entry.block_width, entry.block_height, ctx, ad, ctx_data);
                    
                    auto apply_second_order_reversal = [&]() {
                        // Reverse the residual re-prediction on the meter stream
                        // (the first width*height samples). The coded stream is
                        // residuals-of-residuals; this restores the primary
                        // residuals before the primary predictor runs.
                        if (resid_pid == analyzer::ResidualPredictorId::None) return;
                        const uint32_t bw_ = entry.block_width;
                        const uint32_t bh_ = entry.block_height;
                        rgrid.width = bw_ + 2;
                        rgrid.height = bh_ + 2;
                        rgrid.data.assign(rgrid.width * rgrid.height, 0);

                        if (resid_pid == analyzer::ResidualPredictorId::Average ||
                            resid_pid == analyzer::ResidualPredictorId::Median) {
                            size_t i = 0;
                            for (uint32_t y = 0; y < bh_; ++y) {
                                for (uint32_t x = 0; x < bw_; ++x) {
                                    int32_t wv = (x > 0) ? rgrid.data[(y + 1) * rgrid.width + x] : 0;
                                    int32_t nv = (y > 0) ? rgrid.data[y * rgrid.width + (x + 1)] : 0;
                                    int32_t nwv = (x > 0 && y > 0) ? rgrid.data[y * rgrid.width + x] : 0;
                                    int32_t p = 0;
                                    if (resid_pid == analyzer::ResidualPredictorId::Average) {
                                        if (x > 0 && y > 0) p = wv / 2 + nv / 2;
                                        else if (x > 0) p = wv;
                                        else if (y > 0) p = nv;
                                    } else {
                                        if (x > 0 && y > 0) {
                                            p = std::max(std::min(wv, nv), std::min(std::max(wv, nv), nwv));
                                        } else if (x > 0) {
                                            p = wv;
                                        } else if (y > 0) {
                                            p = nv;
                                        }
                                    }
                                    rgrid.data[(y + 1) * rgrid.width + (x + 1)] = decoded_residuals[i++] + p;
                                }
                            }
                        } else {
                            partition::MutableBlockView rview;
                            rview.grid = &rgrid;
                            rview.x_offset = 1;
                            rview.y_offset = 1;
                            rview.width = bw_;
                            rview.height = bh_;
                            resid_predictor->decode(decoded_residuals, decoded_resid_parameters, rview);
                        }

                        for (uint32_t y = 0; y < bh_; ++y) {
                            for (uint32_t x = 0; x < bw_; ++x) {
                                decoded_residuals[y * bw_ + x] = rgrid.data[(y + 1) * rgrid.width + (x + 1)];
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
                    
                    if (ctx.pipeline_type == analyzer::PipelineType::Wavelet) {
                        transform::CDF53Transform::inverse_2d(decoded_residuals, entry.block_width, entry.block_height, max_levels);
                        for (uint32_t y = 0; y < entry.block_height; ++y) {
                            for (uint32_t x = 0; x < entry.block_width; ++x) {
                                mbv.set(x, y, decoded_residuals[y * entry.block_width + x]);
                            }
                        }
                    } else {
                        apply_second_order_reversal();
                        if (is_split_precision) {
                            uint32_t num_samples = entry.block_width * entry.block_height;
                            m_res.assign(decoded_residuals.begin(), decoded_residuals.begin() + num_samples);
                            p_res.assign(decoded_residuals.begin() + num_samples, decoded_residuals.end());
                            
                            m_grid.width = sgrid.width; m_grid.height = sgrid.height;
                            p_grid.width = sgrid.width; p_grid.height = sgrid.height;
                            m_grid.data.resize(sgrid.width * sgrid.height, 0); p_grid.data.resize(sgrid.width * sgrid.height, 0);
                            
                            // Copy context from the main grid into temp grids
                            uint32_t start_x = (mbv.x_offset > 0) ? mbv.x_offset - 1 : 0;
                            uint32_t start_y = (mbv.y_offset > 0) ? mbv.y_offset - 1 : 0;
                            
                            for (uint32_t y = start_y; y < mbv.y_offset + mbv.height; ++y) {
                                for (uint32_t x = start_x; x < mbv.x_offset + mbv.width; ++x) {
                                    if (y >= mbv.y_offset && x >= mbv.x_offset) continue;
                                    int32_t z = mbv.grid->get(x, y);
                                    m_grid.data[y * m_grid.width + x] = z / ctx.precision_multiplier;
                                    p_grid.data[y * p_grid.width + x] = z % ctx.precision_multiplier;
                                }
                            }
                            
                            partition::MutableBlockView m_view = mbv;
                            m_view.grid = &m_grid;
                            
                            partition::MutableBlockView p_view = mbv;
                            p_view.grid = &p_grid;
                            
                            predictor->decode(m_res, decoded_parameters, m_view);
                            if (prec_identity) {
                                // Raw passthrough: the decoded precision stream
                                // IS the digit plane (no prediction to reverse).
                                for (uint32_t y = 0; y < entry.block_height; ++y) {
                                    for (uint32_t x = 0; x < entry.block_width; ++x) {
                                        p_view.set(x, y, p_res[y * entry.block_width + x]);
                                    }
                                }
                            } else {
                                prec_predictor->decode(p_res, decoded_parameters, p_view);
                            }
                            
                            for (uint32_t y = 0; y < entry.block_height; ++y) {
                                for (uint32_t x = 0; x < entry.block_width; ++x) {
                                    int32_t m = m_view.get(x, y);
                                    int32_t p = p_view.get(x, y);
                                    mbv.set(x, y, m * ctx.precision_multiplier + p);
                                }
                            }
                        } else {
                            predictor->decode(decoded_residuals, decoded_parameters, mbv);
                        }
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
        }
    );
    
    result.blocks_decoded = blocks_decoded;
    return result;
}

} // namespace xtm::coding
