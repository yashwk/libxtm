#include "DecodeCmd.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/io/GDALWriter.hpp"
#include "xtm/terrain/Quantization.hpp"
#include "xtm/partition/Block.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/predictor/Predictors.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

namespace xtm::cli {

int run_decode(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: xtm decode <input.xtm> -o <output.tif>\n";
        return 1;
    }
    
    std::string input_file = argv[1];
    std::string output_file = "";
    int rx = 0, ry = 0, rw = 0, rh = 0;
    bool has_roi = false;
    coding::ContextModel model = coding::ContextModel::Simple;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--region" && i + 4 < argc) {
            rx = std::stoi(argv[++i]);
            ry = std::stoi(argv[++i]);
            rw = std::stoi(argv[++i]);
            rh = std::stoi(argv[++i]);
            has_roi = true;
        } else if (arg == "--context" && i + 1 < argc) {
            std::string ctype = argv[++i];
            if (ctype == "simple") model = coding::ContextModel::Simple;
            else if (ctype == "extended") model = coding::ContextModel::Extended;
            else {
                std::cerr << "Unknown context model: " << ctype << "\n";
                return 1;
            }
        }
    }
    
    if (output_file.empty()) {
        std::cerr << "Error: Output file (-o) is required.\n";
        return 1;
    }
    
    try {
        std::cout << "Opening XTM container: " << input_file << "...\n";
        auto start_time = std::chrono::high_resolution_clock::now();
        
        container::XtmReader reader(input_file);
        const auto& header = reader.get_header();
        const auto& index = reader.get_index();
        
        if (!has_roi) {
            rw = header.grid_width;
            rh = header.grid_height;
        }
        
        std::cout << "Target ROI: x=" << rx << ", y=" << ry << ", w=" << rw << ", h=" << rh << "\n";
        
        terrain::IntGrid roi_grid;
        roi_grid.width = rw;
        roi_grid.height = rh;
        roi_grid.data.resize(rw * rh, 0);
        roi_grid.nodata_mask.resize(rw * rh, false);
        
        predictor::LeftPredictor p_left;
        predictor::AbovePredictor p_above;
        predictor::AveragePredictor p_avg;
        predictor::GradientPredictor p_grad;
        predictor::JpegLsPredictor p_jpegls;
        predictor::PlanePredictor p_plane;
        predictor::GapPredictor p_gap;
        predictor::AdaptiveGradientPredictor p_adap_grad;
        predictor::LeastSquaresPredictor p_least_squares;
        predictor::SecondOrderPredictor p_second_order;
        predictor::LocalSlopePredictor p_local_slope;
        
        std::vector<const predictor::Predictor*> predictors_list = {
            &p_grad, &p_left, &p_above, &p_avg, &p_jpegls, &p_plane, &p_gap, &p_adap_grad, &p_least_squares, &p_second_order, &p_local_slope
        };
        
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
        
        auto worker = [&]() {
            uint32_t local_blocks_decoded = 0;
            
            while (true) {
                uint32_t idx = next_superblock_idx.fetch_add(1);
                if (idx >= num_superblocks_total) break;
                
                std::uint32_t sy = start_sy + (idx / num_superblocks_x) * superblock_size;
                std::uint32_t sx = start_sx + (idx % num_superblocks_x) * superblock_size;
                
                if (sy >= header.grid_height || sx >= header.grid_width) continue;
                
                terrain::IntGrid sgrid;
                sgrid.width = std::min(superblock_size, header.grid_width - sx);
                sgrid.height = std::min(superblock_size, header.grid_height - sy);
                sgrid.data.resize(sgrid.width * sgrid.height, 0);
                sgrid.nodata_mask.resize(sgrid.width * sgrid.height, false);
                
                std::vector<container::BlockIndexEntry> sblocks;
                for (const auto& entry : index) {
                    if (entry.block_x >= sx && entry.block_x < sx + sgrid.width &&
                        entry.block_y >= sy && entry.block_y < sy + sgrid.height) {
                        sblocks.push_back(entry);
                    }
                }
                
                std::sort(sblocks.begin(), sblocks.end(), [](const auto& a, const auto& b) {
                    if (a.block_y != b.block_y) return a.block_y < b.block_y;
                    return a.block_x < b.block_x;
                });
                
                for (const auto& entry : sblocks) {
                    local_blocks_decoded++;
                    auto bitstream = reader.read_block(entry);
                    coding::BitReader br(bitstream);
                    
                    uint32_t predictor_idx = br.read_bits(8);
                    const predictor::Predictor* predictor = predictors_list[predictor_idx];
                    
                    bool use_wavelet = br.read_bits(1) != 0;
                    
                    uint32_t num_params = br.read_bits(8);
                    predictor::PredictionResult decoded_res;
                    for (uint32_t i = 0; i < num_params; ++i) {
                        uint32_t p_bits = br.read_bits(32);
                        int32_t p;
                        std::memcpy(&p, &p_bits, sizeof(int32_t));
                        decoded_res.parameters.push_back(p);
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
                    
                    uint32_t max_levels = 3;
                    uint32_t dim = std::min(entry.block_width, entry.block_height);
                    while (max_levels > 0 && dim < (1u << max_levels)) {
                        max_levels--;
                    }
                    if (!use_wavelet) max_levels = 0;
                    
                    auto extract_subbands_local = [&](std::vector<std::pair<uint32_t, uint32_t>>& ll,
                                                      std::vector<std::pair<uint32_t, uint32_t>>& lh,
                                                      std::vector<std::pair<uint32_t, uint32_t>>& hl,
                                                      std::vector<std::pair<uint32_t, uint32_t>>& hh) {
                        for (uint32_t y = 0; y < entry.block_height; ++y) {
                            for (uint32_t x = 0; x < entry.block_width; ++x) {
                                uint32_t sb = 0;
                                for (uint32_t level = 1; level <= max_levels; ++level) {
                                    uint32_t cur_w = entry.block_width >> (level - 1);
                                    uint32_t cur_h = entry.block_height >> (level - 1);
                                    uint32_t half_w = cur_w / 2;
                                    uint32_t half_h = cur_h / 2;
                                    
                                    if (x >= half_w || y >= half_h) {
                                        if (x >= half_w && y >= half_h) sb = 3;
                                        else if (x >= half_w) sb = 2;
                                        else sb = 1;
                                        break;
                                    }
                                }
                                if (sb == 0) ll.push_back({x, y});
                                else if (sb == 1) lh.push_back({x, y});
                                else if (sb == 2) hl.push_back({x, y});
                                else hh.push_back({x, y});
                            }
                        }
                    };
                    
                    std::vector<std::pair<uint32_t, uint32_t>> coords_LL, coords_LH, coords_HL, coords_HH;
                    extract_subbands_local(coords_LL, coords_LH, coords_HL, coords_HH);
                    
                    coding::ArithmeticDecoder ad(br);
                    std::unordered_map<coding::Context, coding::FrequencyTable> context_tables;
                    coding::FrequencyTable run_table(256);
                    coding::FrequencyTable uniform_bit(2);
                    
                    uint32_t total_symbols = entry.block_width * entry.block_height;
                    decoded_res.residuals.assign(total_symbols, 0);
                    
                    auto decode_subband_symbols = [&](const std::vector<std::pair<uint32_t, uint32_t>>& coords, uint8_t sb_idx) {
                        uint32_t decoded_count = 0;
                        while (decoded_count < coords.size()) {
                            coding::Context ctx;
                            ctx.subband = sb_idx;
                            
                            ctx.neighbour_activity = 0;
                            if (model == coding::ContextModel::Extended && decoded_count > 0) {
                                uint32_t prev_x = coords[decoded_count-1].first;
                                uint32_t prev_y = coords[decoded_count-1].second;
                                if (std::abs(decoded_res.residuals[prev_y * entry.block_width + prev_x]) > 2) {
                                    ctx.neighbour_activity = 1;
                                }
                            }
                            
                            if (context_tables.find(ctx) == context_tables.end()) {
                                context_tables.emplace(ctx, coding::FrequencyTable(33));
                            }
                            auto& freqs = context_tables.at(ctx);
                            
                            uint32_t mag_class = ad.decode(freqs);
                            freqs.increment(mag_class);
                            
                            if (mag_class == 0) {
                                uint32_t run_len_minus_1 = ad.decode(run_table);
                                run_table.increment(run_len_minus_1);
                                uint32_t run_len = run_len_minus_1 + 1;
                                decoded_count += run_len;
                            } else {
                                uint32_t remainder = 0;
                                if (mag_class > 1) {
                                    for (int i = mag_class - 2; i >= 0; --i) {
                                        uint32_t bit = ad.decode(uniform_bit);
                                        remainder |= (bit << i);
                                    }
                                }
                                uint32_t zz = (1u << (mag_class - 1)) | remainder;
                                int32_t val = coding::zigzag_decode(zz);
                                uint32_t x = coords[decoded_count].first;
                                uint32_t y = coords[decoded_count].second;
                                decoded_res.residuals[y * entry.block_width + x] = val;
                                decoded_count++;
                            }
                        }
                    };
                    
                    decode_subband_symbols(coords_LL, 0);
                    decode_subband_symbols(coords_LH, 1);
                    decode_subband_symbols(coords_HL, 2);
                    decode_subband_symbols(coords_HH, 3);
                    
                    bool wavelet_first = (header.flags & container::XtmHeader::FLAG_WAVELET_FIRST) != 0;
                    
                    partition::MutableBlockView mbv;
                    mbv.grid = &sgrid;
                    mbv.x_offset = entry.block_x - sx;
                    mbv.y_offset = entry.block_y - sy;
                    mbv.width = entry.block_width;
                    mbv.height = entry.block_height;
                    
                    if (wavelet_first) {
                        // We must decode into a local grid because the encoder predicted on a local block
                        terrain::IntGrid local_grid;
                        local_grid.width = entry.block_width;
                        local_grid.height = entry.block_height;
                        local_grid.data.resize(entry.block_width * entry.block_height, 0);
                        
                        partition::MutableBlockView local_mbv;
                        local_mbv.grid = &local_grid;
                        local_mbv.x_offset = 0;
                        local_mbv.y_offset = 0;
                        local_mbv.width = entry.block_width;
                        local_mbv.height = entry.block_height;
                        
                        // Decode Predictor to get wavelet coefficients, then Inverse Wavelet
                        predictor->decode(decoded_res, local_mbv);
                        
                        if (max_levels > 0) {
                            std::vector<int32_t> wv_coeffs = local_grid.data;
                            
                            transform::CDF53Transform::inverse_2d(wv_coeffs, entry.block_width, entry.block_height, max_levels);
                            
                            for (uint32_t y = 0; y < entry.block_height; ++y) {
                                for (uint32_t x = 0; x < entry.block_width; ++x) {
                                    mbv.set(x, y, wv_coeffs[y * entry.block_width + x]);
                                }
                            }
                        } else {
                            for (uint32_t y = 0; y < entry.block_height; ++y) {
                                for (uint32_t x = 0; x < entry.block_width; ++x) {
                                    mbv.set(x, y, local_mbv.get(x, y));
                                }
                            }
                        }
                    } else {
                        // Inverse Wavelet to get predictor residuals, then Decode Predictor
                        if (max_levels > 0) {
                            transform::CDF53Transform::inverse_2d(decoded_res.residuals, entry.block_width, entry.block_height, max_levels);
                        }
                        
                        predictor->decode(decoded_res, mbv);
                    }
                }
                
                // Copy sgrid into roi_grid
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
        };
        
        uint32_t num_threads = std::thread::hardware_concurrency();
        std::vector<std::thread> threads;
        for (uint32_t i = 0; i < num_threads; ++i) {
            threads.emplace_back(worker);
        }
        for (auto& t : threads) {
            t.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end_time - start_time;
        std::cout << "Decoded " << blocks_decoded << " blocks in " << diff.count() << " seconds.\n";
        
        std::cout << "Dequantizing and writing to " << output_file << "...\n";
        
        GeoTransform gt;
        double full_pixel_width = (header.max_x - header.min_x) / header.grid_width;
        double full_pixel_height = (header.min_y - header.max_y) / header.grid_height; // Negative
        
        gt.origin_x = header.min_x + rx * full_pixel_width;
        gt.origin_y = header.max_y + ry * full_pixel_height;
        gt.pixel_width = full_pixel_width;
        gt.pixel_height = full_pixel_height;
        
        std::optional<float> nodata_val = std::nullopt;
        if (header.flags & container::XtmHeader::FLAG_HAS_NODATA) {
            nodata_val = header.nodata_value;
        }
        TerrainBuffer out_buffer = terrain::dequantize(roi_grid, header.res_x, nodata_val, gt);
        io::write_gdal(output_file, out_buffer.view(), header.epsg_crs);
        
        std::cout << "Successfully exported TIFF!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

} // namespace xtm::cli
