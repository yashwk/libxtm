#include "xtm/analyzer/Analyzer.hpp"
#include "xtm/analyzer/Statistics.hpp"
#include "xtm/analyzer/Selector.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/terrain/Quantization.hpp"
#include "xtm/predictor/Predictors.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <limits>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>

namespace xtm::analyzer {

static void compute_correlation(const int32_t* data, uint32_t width, uint32_t height, double& horiz, double& vert, double& diag) {
    if (width < 2 || height < 2) return;
    
    double mean = 0;
    size_t count = width * height;
    for (size_t i = 0; i < count; ++i) mean += data[i];
    mean /= count;
    
    double var_sum = 0;
    for (size_t i = 0; i < count; ++i) {
        double d = data[i] - mean;
        var_sum += d * d;
    }
    
    if (var_sum == 0) {
        horiz = vert = diag = 1.0;
        return;
    }
    
    double cov_h = 0, cov_v = 0, cov_d = 0;
    
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            double d0 = data[y * width + x] - mean;
            if (x + 1 < width) cov_h += d0 * (data[y * width + (x + 1)] - mean);
            if (y + 1 < height) cov_v += d0 * (data[(y + 1) * width + x] - mean);
            if (x + 1 < width && y + 1 < height) cov_d += d0 * (data[(y + 1) * width + (x + 1)] - mean);
        }
    }
    
    double var_h = var_sum * (width - 1) * height / count;
    double var_v = var_sum * width * (height - 1) / count;
    double var_d = var_sum * (width - 1) * (height - 1) / count;
    
    horiz = cov_h / var_h;
    vert = cov_v / var_v;
    diag = cov_d / var_d;
}

AnalysisReport analyze_terrain(const TerrainView& view, double scale, coding::ContextModel model) {
    AnalysisReport report{};
    report.width = view.width;
    report.height = view.height;
    report.sample_count = static_cast<std::size_t>(view.width) * view.height;

    if (report.sample_count == 0) return report;

    std::vector<float> valid_data;
    valid_data.reserve(report.sample_count);

    double sum = 0.0;
    float min_val = std::numeric_limits<float>::max();
    float max_val = std::numeric_limits<float>::lowest();
    std::unordered_set<float> unique_vals;

    for (std::uint32_t y = 0; y < view.height; ++y) {
        for (std::uint32_t x = 0; x < view.width; ++x) {
            float val = view.get(x, y);
            if (view.nodata_value && val == *view.nodata_value) continue;

            valid_data.push_back(val);
            sum += val;
            min_val = std::min(min_val, val);
            max_val = std::max(max_val, val);
            unique_vals.insert(val);
        }
    }

    report.elevation.min_val = min_val;
    report.elevation.max_val = max_val;
    report.elevation.unique_values = unique_vals.size();

    if (!valid_data.empty()) {
        report.elevation.mean = sum / valid_data.size();
        double variance_sum = 0.0;
        for (float val : valid_data) {
            double diff = val - report.elevation.mean;
            variance_sum += diff * diff;
        }
        report.elevation.stddev = std::sqrt(variance_sum / valid_data.size());
        report.elevation.shannon_entropy = calculate_entropy(valid_data);
    }

    terrain::IntGrid grid = terrain::quantize(view, scale);
    
    std::vector<int32_t> m_vals, dm_vals, cm_vals, mm_vals;
    m_vals.reserve(grid.data.size());
    if (scale <= 0.1) dm_vals.reserve(grid.data.size());
    if (scale <= 0.01) cm_vals.reserve(grid.data.size());
    if (scale <= 0.001) mm_vals.reserve(grid.data.size());

    double s_inv = std::round(1.0 / scale);
    int32_t scale_factor = static_cast<int32_t>(s_inv);

    for (size_t i = 0; i < grid.data.size(); ++i) {
        if (!grid.nodata_mask.empty() && grid.nodata_mask[i]) continue;
        int32_t v = std::abs(grid.data[i]);
        if (scale_factor == 1) {
            m_vals.push_back(v);
        } else if (scale_factor == 10) {
            m_vals.push_back(v / 10);
            dm_vals.push_back(v % 10);
        } else if (scale_factor == 100) {
            m_vals.push_back(v / 100);
            dm_vals.push_back((v / 10) % 10);
            cm_vals.push_back(v % 10);
        } else if (scale_factor >= 1000) {
            m_vals.push_back(v / 1000);
            dm_vals.push_back((v / 100) % 10);
            cm_vals.push_back((v / 10) % 10);
            mm_vals.push_back(v % 10);
        }
    }

    if (!m_vals.empty()) report.precision.meter_entropy = calculate_entropy(m_vals);
    if (!dm_vals.empty()) report.precision.decimeter_entropy = calculate_entropy(dm_vals);
    if (!cm_vals.empty()) report.precision.centimeter_entropy = calculate_entropy(cm_vals);
    if (!mm_vals.empty()) report.precision.millimeter_entropy = calculate_entropy(mm_vals);
    
    compute_correlation(grid.data.data(), grid.width, grid.height, 
                        report.correlation_stats.raw_horizontal, 
                        report.correlation_stats.raw_vertical, 
                        report.correlation_stats.raw_diagonal);

    // Multithreaded analysis over 512x512 superblocks
    std::uint32_t superblock_size = 512;
    std::uint32_t num_superblocks_x = (grid.width + superblock_size - 1) / superblock_size;
    std::uint32_t num_superblocks_y = (grid.height + superblock_size - 1) / superblock_size;
    std::uint32_t num_superblocks_total = num_superblocks_x * num_superblocks_y;

    std::atomic<uint32_t> next_superblock_idx(0);
    std::mutex stats_mutex;
    
    // Global stats aggregators
    std::unordered_map<int32_t, std::size_t> glob_left_counts, glob_above_counts, glob_avg_counts, glob_grad_counts, glob_jpegls_counts, glob_plane_counts, glob_gap_counts, glob_adap_grad_counts, glob_least_squares_counts, glob_second_order_counts, glob_local_slope_counts;
    std::unordered_map<int32_t, std::size_t> block64_left_counts, block64_above_counts, block64_avg_counts, block64_grad_counts, block64_jpegls_counts, block64_plane_counts, block64_gap_counts, block64_adap_grad_counts, block64_least_squares_counts, block64_second_order_counts, block64_local_slope_counts;
    
    double total_adaptive_64_bits = 0;
    double quad_bits_total = 0;
    double dwt_bits_total = 0;
    double quad_overhead_total = 0;
    std::size_t total_quad_leaves = 0;
    
    QuadtreeStats q_stats;
    PredictorUsage p_usage;
    SubbandStats w_stats;
    ContextStats c_stats;
    ResidualDistributionStats r_dist_stats;
    
    std::size_t ll_zeros=0, lh_zeros=0, hl_zeros=0, hh_zeros=0;
    std::unordered_map<int32_t, std::size_t> ll_map, lh_map, hl_map, hh_map;
    std::size_t ll_count=0, lh_count=0, hl_count=0, hh_count=0;
    std::size_t total_zero_runs=0, total_zero_run_length=0;
    std::size_t total_symbols=0, total_contexts=0;
    std::unordered_map<coding::Context, std::size_t> global_context_sizes;
    
    
    std::size_t easy_count=0, medium_count=0, hard_count=0;
    double easy_entropy=0, medium_entropy=0, hard_entropy=0;
    std::size_t exact=0, w1=0, w2=0, w5=0, w10=0, res_count=0;
    std::unordered_map<int32_t, std::size_t> histogram;

    auto worker = [&]() {
        predictor::PredictorBank bank;
        
        std::vector<const predictor::Predictor*> pred_list = bank.ordered();
        PredictorSelector selector(pred_list);
        
        std::unordered_map<int32_t, std::size_t> l_glob_left, l_glob_above, l_glob_avg, l_glob_grad, l_glob_jpegls, l_glob_plane, l_glob_gap, l_glob_adap_grad, l_glob_least_squares, l_glob_second_order, l_glob_local_slope;
        std::unordered_map<int32_t, std::size_t> l_64_left, l_64_above, l_64_avg, l_64_grad, l_64_jpegls, l_64_plane, l_64_gap, l_64_adap_grad, l_64_least_squares, l_64_second_order, l_64_local_slope;
        
        double l_total_adaptive_64_bits = 0;
        double l_quad_bits = 0;
        double l_dwt_bits = 0;
        double l_quad_overhead = 0;
        std::size_t l_quad_leaves = 0;
        
        QuadtreeStats l_q_stats;
        PredictorUsage l_p_usage;
        SubbandStats l_w_stats;
        ContextStats l_c_stats;
        ResidualDistributionStats l_r_dist_stats;
        
        std::size_t l_ll_zeros=0, l_lh_zeros=0, l_hl_zeros=0, l_hh_zeros=0;
        std::unordered_map<int32_t, std::size_t> l_ll_map, l_lh_map, l_hl_map, l_hh_map;
        std::size_t l_ll_count=0, l_lh_count=0, l_hl_count=0, l_hh_count=0;
        std::size_t l_total_zero_runs=0, l_total_zero_run_length=0;
        std::size_t l_total_symbols=0, l_total_contexts=0;
        std::unordered_map<coding::Context, std::size_t> l_global_context_sizes;
        
        std::size_t l_easy_count=0, l_medium_count=0, l_hard_count=0;
        double l_easy_entropy=0, l_medium_entropy=0, l_hard_entropy=0;
        std::size_t l_exact=0, l_w1=0, l_w2=0, l_w5=0, l_w10=0, l_res_count=0;
        std::unordered_map<int32_t, std::size_t> l_histogram;
        
        while (true) {
            uint32_t idx = next_superblock_idx.fetch_add(1);
            if (idx >= num_superblocks_total) break;
            
            std::uint32_t sy = (idx / num_superblocks_x) * superblock_size;
            std::uint32_t sx = (idx % num_superblocks_x) * superblock_size;
            
            terrain::IntGrid sgrid;
            sgrid.width = std::min(superblock_size, grid.width - sx);
            sgrid.height = std::min(superblock_size, grid.height - sy);
            sgrid.data.resize(sgrid.width * sgrid.height);
            sgrid.nodata_mask.resize(sgrid.width * sgrid.height, false);
            for (std::uint32_t y = 0; y < sgrid.height; ++y) {
                for (std::uint32_t x = 0; x < sgrid.width; ++x) {
                    uint32_t s_idx = y * sgrid.width + x;
                    uint32_t c_idx = (sy + y) * grid.width + (sx + x);
                    sgrid.data[s_idx] = grid.data[c_idx];
                    if (!grid.nodata_mask.empty()) sgrid.nodata_mask[s_idx] = grid.nodata_mask[c_idx];
                }
            }
            
            partition::BlockView full_sb{&sgrid, 0, 0, sgrid.width, sgrid.height};
            
            auto accumulate = [](const std::vector<int32_t>& res, std::unordered_map<int32_t, std::size_t>& counts) {
                for (int32_t r : res) counts[r]++;
            };
            
            predictor::PredictionResult l_glob;
            bank.left->encode(full_sb, l_glob); accumulate(l_glob.residuals, l_glob_left);
            predictor::PredictionResult a_glob;
            bank.above->encode(full_sb, a_glob); accumulate(a_glob.residuals, l_glob_above);
            predictor::PredictionResult v_glob;
            bank.average->encode(full_sb, v_glob); accumulate(v_glob.residuals, l_glob_avg);
            predictor::PredictionResult g_glob;
            bank.gradient->encode(full_sb, g_glob); accumulate(g_glob.residuals, l_glob_grad);
            predictor::PredictionResult j_glob;
            bank.jpegls->encode(full_sb, j_glob); accumulate(j_glob.residuals, l_glob_jpegls);
            predictor::PredictionResult pl_glob;
            bank.plane->encode(full_sb, pl_glob); accumulate(pl_glob.residuals, l_glob_plane);
            predictor::PredictionResult gp_glob;
            bank.gap->encode(full_sb, gp_glob); accumulate(gp_glob.residuals, l_glob_gap);
            predictor::PredictionResult ag_glob;
            bank.adaptive_gradient->encode(full_sb, ag_glob); accumulate(ag_glob.residuals, l_glob_adap_grad);
            predictor::PredictionResult ls_glob;
            bank.least_squares->encode(full_sb, ls_glob); accumulate(ls_glob.residuals, l_glob_least_squares);
            predictor::PredictionResult so_glob;
            bank.second_order->encode(full_sb, so_glob); accumulate(so_glob.residuals, l_glob_second_order);
            predictor::PredictionResult lsp_glob;
            bank.local_slope->encode(full_sb, lsp_glob); accumulate(lsp_glob.residuals, l_glob_local_slope);
            
            const terrain::IntGrid& const_sgrid = sgrid;
            auto blocks64 = partition::FixedGridPartitioner::partition(const_sgrid, 64);
            for (const auto& b : blocks64) {
                predictor::PredictionResult scratch;
                bank.left->encode(b, scratch); accumulate(scratch.residuals, l_64_left);
                bank.above->encode(b, scratch); accumulate(scratch.residuals, l_64_above);
                bank.average->encode(b, scratch); accumulate(scratch.residuals, l_64_avg);
                bank.gradient->encode(b, scratch); accumulate(scratch.residuals, l_64_grad);
                bank.jpegls->encode(b, scratch); accumulate(scratch.residuals, l_64_jpegls);
                bank.plane->encode(b, scratch); accumulate(scratch.residuals, l_64_plane);
                bank.gap->encode(b, scratch); accumulate(scratch.residuals, l_64_gap);
                bank.adaptive_gradient->encode(b, scratch); accumulate(scratch.residuals, l_64_adap_grad);
                bank.least_squares->encode(b, scratch); accumulate(scratch.residuals, l_64_least_squares);
                bank.second_order->encode(b, scratch); accumulate(scratch.residuals, l_64_second_order);
                bank.local_slope->encode(b, scratch); accumulate(scratch.residuals, l_64_local_slope);
                
                auto res = selector.select(b);
                l_total_adaptive_64_bits += res.total_bits;
                
                double block_entropy = calculate_entropy(res.best_encoded.residuals);
                if (block_entropy < 3.0) { l_easy_count++; l_easy_entropy += block_entropy; }
                else if (block_entropy < 7.0) { l_medium_count++; l_medium_entropy += block_entropy; }
                else { l_hard_count++; l_hard_entropy += block_entropy; }
                
            }
            
            double q_bits = 0.0;
            auto quad_leaves = partition::QuadtreePartitioner::partition(sgrid, 512, 64, selector, q_bits);
            l_quad_bits += q_bits;
            l_quad_leaves += quad_leaves.size();
            
            double sum_leaf_bits = 0.0;
            for (auto& leaf : quad_leaves) {
                sum_leaf_bits += leaf.selection.total_bits;
            }
            l_quad_overhead += (q_bits - sum_leaf_bits);
            
            for (auto& leaf : quad_leaves) {
                double mag_sum = 0;
                for (int32_t r : leaf.selection.best_encoded.residuals) {
                    mag_sum += std::abs(r);
                    l_histogram[r]++;
                    l_res_count++;
                    int32_t abs_r = std::abs(r);
                    if (abs_r == 0) l_exact++;
                    if (abs_r <= 1) l_w1++;
                    if (abs_r <= 2) l_w2++;
                    if (abs_r <= 5) l_w5++;
                    if (abs_r <= 10) l_w10++;
                }
                if (!leaf.selection.best_encoded.residuals.empty()) {
                    mag_sum /= leaf.selection.best_encoded.residuals.size();
                }
                
                if (leaf.selection.best_predictor == bank.left.get()) { l_p_usage.left_count++; l_p_usage.left_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.above.get()) { l_p_usage.above_count++; l_p_usage.above_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.average.get()) { l_p_usage.average_count++; l_p_usage.average_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.gradient.get()) { l_p_usage.gradient_count++; l_p_usage.gradient_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.jpegls.get()) { l_p_usage.jpegls_count++; l_p_usage.jpegls_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.plane.get()) { l_p_usage.plane_count++; l_p_usage.plane_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.gap.get()) { l_p_usage.gap_count++; l_p_usage.gap_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.adaptive_gradient.get()) { l_p_usage.adaptive_gradient_count++; l_p_usage.adaptive_gradient_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.least_squares.get()) { l_p_usage.least_squares_count++; l_p_usage.least_squares_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.second_order.get()) { l_p_usage.second_order_count++; l_p_usage.second_order_mag_sum += mag_sum; }
                else if (leaf.selection.best_predictor == bank.local_slope.get()) { l_p_usage.local_slope_count++; l_p_usage.local_slope_mag_sum += mag_sum; }

                uint32_t w = leaf.block.width;
                if (w == 512) l_q_stats.size_512_count++;
                else if (w == 256) l_q_stats.size_256_count++;
                else if (w == 128) l_q_stats.size_128_count++;
                else l_q_stats.size_64_count++;
                uint32_t max_levels = 3;
                uint32_t dim = std::min(leaf.block.width, leaf.block.height);
                while (max_levels > 0 && dim < (1u << max_levels)) {
                    max_levels--;
                }
                
                std::vector<int32_t> data = leaf.selection.best_encoded.residuals;
                if (max_levels > 0) {
                    transform::CDF53Transform::forward_2d(data, leaf.block.width, leaf.block.height, max_levels);
                }
                
                auto symbols = coding::generate_symbols(data, leaf.block.width, leaf.block.height, max_levels, model);
                
                std::unordered_set<coding::Context> block_contexts;
                
                double mag_class_bits = 0.0;
                double remainder_bits = 0.0;
                double run_bits = 0.0;
                uint32_t num_symbols = symbols.size();
                uint32_t num_zero_runs = 0;
                
                std::vector<int32_t> mag_classes;
                std::vector<int32_t> run_lengths;
                
                for (const auto& sym : symbols) {
                    block_contexts.insert(sym.context);
                    l_total_symbols++;
                    
                    mag_classes.push_back(sym.magnitude_class);
                    if (sym.magnitude_class == 0) {
                        run_lengths.push_back(sym.run_length);
                        num_zero_runs++;
                        
                        l_total_zero_runs++;
                        l_total_zero_run_length += sym.run_length;
                        l_w_stats.max_zero_run = std::max(l_w_stats.max_zero_run, static_cast<std::size_t>(sym.run_length));
                        
                        if (sym.context.subband == 0) l_ll_zeros += sym.run_length;
                        else if (sym.context.subband == 1) l_lh_zeros += sym.run_length;
                        else if (sym.context.subband == 2) l_hl_zeros += sym.run_length;
                        else if (sym.context.subband == 3) l_hh_zeros += sym.run_length;
                        
                        for (uint32_t i=0; i < sym.run_length; ++i) {
                            if (sym.context.subband == 0) { l_ll_map[0]++; l_ll_count++; }
                            else if (sym.context.subband == 1) { l_lh_map[0]++; l_lh_count++; }
                            else if (sym.context.subband == 2) { l_hl_map[0]++; l_hl_count++; }
                            else if (sym.context.subband == 3) { l_hh_map[0]++; l_hh_count++; }
                        }
                    } else {
                        if (sym.magnitude_class > 1) {
                            remainder_bits += (sym.magnitude_class - 1);
                        }
                        
                        uint32_t zz = (1u << (sym.magnitude_class - 1)) | sym.remainder;
                        int32_t val = (zz >> 1) ^ -(zz & 1); // inverse zigzag
                        
                        if (sym.context.subband == 0) { l_ll_map[val]++; l_ll_count++; }
                        else if (sym.context.subband == 1) { l_lh_map[val]++; l_lh_count++; }
                        else if (sym.context.subband == 2) { l_hl_map[val]++; l_hl_count++; }
                        else if (sym.context.subband == 3) { l_hh_map[val]++; l_hh_count++; }
                    }
                    
                    l_global_context_sizes[sym.context]++;
                }
                
                l_total_contexts += block_contexts.size();
                
                mag_class_bits = calculate_entropy(mag_classes) * num_symbols;
                if (!run_lengths.empty()) {
                    run_bits = calculate_entropy(run_lengths) * num_zero_runs;
                }
                
                double c_id = 8.0;
                double c_params = leaf.selection.best_encoded.parameters.size() * 32.0;
                double c_residual = mag_class_bits + remainder_bits + run_bits;
                
                l_dwt_bits += c_id + c_params + c_residual;
            }
        }
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        auto merge_counts = [](std::unordered_map<int32_t, std::size_t>& dst, const std::unordered_map<int32_t, std::size_t>& src) {
            for (const auto& kv : src) dst[kv.first] += kv.second;
        };
        merge_counts(glob_left_counts, l_glob_left);
        merge_counts(glob_above_counts, l_glob_above);
        merge_counts(glob_avg_counts, l_glob_avg);
        merge_counts(glob_grad_counts, l_glob_grad);
        merge_counts(glob_jpegls_counts, l_glob_jpegls);
        merge_counts(glob_plane_counts, l_glob_plane);
        merge_counts(glob_gap_counts, l_glob_gap);
        merge_counts(glob_adap_grad_counts, l_glob_adap_grad);
        merge_counts(glob_least_squares_counts, l_glob_least_squares);
        merge_counts(glob_second_order_counts, l_glob_second_order);
        merge_counts(glob_local_slope_counts, l_glob_local_slope);
        
        merge_counts(block64_left_counts, l_64_left);
        merge_counts(block64_above_counts, l_64_above);
        merge_counts(block64_avg_counts, l_64_avg);
        merge_counts(block64_grad_counts, l_64_grad);
        merge_counts(block64_jpegls_counts, l_64_jpegls);
        merge_counts(block64_plane_counts, l_64_plane);
        merge_counts(block64_gap_counts, l_64_gap);
        merge_counts(block64_adap_grad_counts, l_64_adap_grad);
        merge_counts(block64_least_squares_counts, l_64_least_squares);
        merge_counts(block64_second_order_counts, l_64_second_order);
        merge_counts(block64_local_slope_counts, l_64_local_slope);
        
        total_adaptive_64_bits += l_total_adaptive_64_bits;
        quad_bits_total += l_quad_bits;
        quad_overhead_total += l_quad_overhead;
        dwt_bits_total += l_dwt_bits;
        total_quad_leaves += l_quad_leaves;
        
        q_stats.size_512_count += l_q_stats.size_512_count;
        q_stats.size_256_count += l_q_stats.size_256_count;
        q_stats.size_128_count += l_q_stats.size_128_count;
        q_stats.size_64_count += l_q_stats.size_64_count;
        
        p_usage.left_count += l_p_usage.left_count; p_usage.left_mag_sum += l_p_usage.left_mag_sum;
        p_usage.above_count += l_p_usage.above_count; p_usage.above_mag_sum += l_p_usage.above_mag_sum;
        p_usage.average_count += l_p_usage.average_count; p_usage.average_mag_sum += l_p_usage.average_mag_sum;
        p_usage.gradient_count += l_p_usage.gradient_count; p_usage.gradient_mag_sum += l_p_usage.gradient_mag_sum;
        p_usage.jpegls_count += l_p_usage.jpegls_count; p_usage.jpegls_mag_sum += l_p_usage.jpegls_mag_sum;
        p_usage.plane_count += l_p_usage.plane_count; p_usage.plane_mag_sum += l_p_usage.plane_mag_sum;
        p_usage.gap_count += l_p_usage.gap_count; p_usage.gap_mag_sum += l_p_usage.gap_mag_sum;
        p_usage.adaptive_gradient_count += l_p_usage.adaptive_gradient_count; p_usage.adaptive_gradient_mag_sum += l_p_usage.adaptive_gradient_mag_sum;
        p_usage.least_squares_count += l_p_usage.least_squares_count; p_usage.least_squares_mag_sum += l_p_usage.least_squares_mag_sum;
        p_usage.second_order_count += l_p_usage.second_order_count; p_usage.second_order_mag_sum += l_p_usage.second_order_mag_sum;
        p_usage.local_slope_count += l_p_usage.local_slope_count; p_usage.local_slope_mag_sum += l_p_usage.local_slope_mag_sum;
        
        ll_zeros += l_ll_zeros; lh_zeros += l_lh_zeros; hl_zeros += l_hl_zeros; hh_zeros += l_hh_zeros;
        
        for (const auto& kv : l_ll_map) { ll_map[kv.first] += kv.second; } ll_count += l_ll_count;
        for (const auto& kv : l_lh_map) { lh_map[kv.first] += kv.second; } lh_count += l_lh_count;
        for (const auto& kv : l_hl_map) { hl_map[kv.first] += kv.second; } hl_count += l_hl_count;
        for (const auto& kv : l_hh_map) { hh_map[kv.first] += kv.second; } hh_count += l_hh_count;

        total_zero_runs += l_total_zero_runs;
        total_zero_run_length += l_total_zero_run_length;
        w_stats.max_zero_run = std::max(w_stats.max_zero_run, l_w_stats.max_zero_run);
        
        total_symbols += l_total_symbols;
        total_contexts += l_total_contexts;
        for (const auto& kv : l_global_context_sizes) {
            global_context_sizes[kv.first] += kv.second;
        }
        
        easy_count += l_easy_count; medium_count += l_medium_count; hard_count += l_hard_count;
        easy_entropy += l_easy_entropy; medium_entropy += l_medium_entropy; hard_entropy += l_hard_entropy;
        exact += l_exact; w1 += l_w1; w2 += l_w2; w5 += l_w5; w10 += l_w10; res_count += l_res_count;
        for (const auto& kv : l_histogram) histogram[kv.first] += kv.second;
    };
    
    uint32_t num_threads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }
    
    report.quadtree_leaves = total_quad_leaves;
    if (res_count > 0) {
        report.predictor_confidence.pct_exact = (double)exact / res_count * 100.0;
        report.predictor_confidence.pct_within_1 = (double)w1 / res_count * 100.0;
        report.predictor_confidence.pct_within_2 = (double)w2 / res_count * 100.0;
        report.predictor_confidence.pct_within_5 = (double)w5 / res_count * 100.0;
        report.predictor_confidence.pct_within_10 = (double)w10 / res_count * 100.0;
    }
    std::size_t total_difficulty = easy_count + medium_count + hard_count;
    if (total_difficulty > 0) {
        report.prediction_difficulty.easy_pct = (double)easy_count / total_difficulty * 100.0;
        report.prediction_difficulty.medium_pct = (double)medium_count / total_difficulty * 100.0;
        report.prediction_difficulty.hard_pct = (double)hard_count / total_difficulty * 100.0;
        if (easy_count > 0) report.prediction_difficulty.easy_avg_entropy = easy_entropy / easy_count;
        if (medium_count > 0) report.prediction_difficulty.medium_avg_entropy = medium_entropy / medium_count;
        if (hard_count > 0) report.prediction_difficulty.hard_avg_entropy = hard_entropy / hard_count;
    }
    for (const auto& kv : histogram) {
        report.residual_histogram.push_back({kv.first, (double)kv.second / res_count * 100.0});
    }
    std::sort(report.residual_histogram.begin(), report.residual_histogram.end());
    report.quadtree_stats = q_stats;
    report.predictor_usage = p_usage;
    
    std::size_t grid_samples = report.sample_count;
    report.global_predictors.left_entropy = calculate_entropy(glob_left_counts, grid_samples);
    report.global_predictors.above_entropy = calculate_entropy(glob_above_counts, grid_samples);
    report.global_predictors.average_entropy = calculate_entropy(glob_avg_counts, grid_samples);
    report.global_predictors.gradient_entropy = calculate_entropy(glob_grad_counts, grid_samples);
    report.global_predictors.jpegls_entropy = calculate_entropy(glob_jpegls_counts, grid_samples);
    report.global_predictors.plane_entropy = calculate_entropy(glob_plane_counts, grid_samples);
    report.global_predictors.gap_entropy = calculate_entropy(glob_gap_counts, grid_samples);
    report.global_predictors.adaptive_gradient_entropy = calculate_entropy(glob_adap_grad_counts, grid_samples);
    report.global_predictors.least_squares_entropy = calculate_entropy(glob_least_squares_counts, grid_samples);
    report.global_predictors.second_order_entropy = calculate_entropy(glob_second_order_counts, grid_samples);
    report.global_predictors.local_slope_entropy = calculate_entropy(glob_local_slope_counts, grid_samples);
    
    report.block64_predictors.left_entropy = calculate_entropy(block64_left_counts, grid_samples);
    report.block64_predictors.above_entropy = calculate_entropy(block64_above_counts, grid_samples);
    report.block64_predictors.average_entropy = calculate_entropy(block64_avg_counts, grid_samples);
    report.block64_predictors.gradient_entropy = calculate_entropy(block64_grad_counts, grid_samples);
    report.block64_predictors.jpegls_entropy = calculate_entropy(block64_jpegls_counts, grid_samples);
    report.block64_predictors.plane_entropy = calculate_entropy(block64_plane_counts, grid_samples);
    report.block64_predictors.gap_entropy = calculate_entropy(block64_gap_counts, grid_samples);
    report.block64_predictors.adaptive_gradient_entropy = calculate_entropy(block64_adap_grad_counts, grid_samples);
    report.block64_predictors.least_squares_entropy = calculate_entropy(block64_least_squares_counts, grid_samples);
    report.block64_predictors.second_order_entropy = calculate_entropy(block64_second_order_counts, grid_samples);
    report.block64_predictors.local_slope_entropy = calculate_entropy(block64_local_slope_counts, grid_samples);
    
    report.adaptive_block64_entropy = total_adaptive_64_bits / grid_samples;
    report.quadtree_entropy = quad_bits_total / grid_samples;
    report.dwt_quadtree_entropy = (dwt_bits_total + quad_overhead_total) / grid_samples;
    
    {
        predictor::PredictorBank temp_bank;
        predictor::PredictionResult temp_res;

        temp_bank.gradient->encode(partition::BlockView{&grid, 0, 0, grid.width, grid.height}, temp_res);
        compute_correlation(temp_res.residuals.data(), grid.width, grid.height, 
                            report.correlation_stats.residual_horizontal, 
                            report.correlation_stats.residual_vertical, 
                            report.correlation_stats.residual_diagonal);
    }
                        
    double res_mean = 0;
    size_t res_zeros = 0;
    std::size_t grad_count = 0;
    for (const auto& kv : glob_grad_counts) {
        if (kv.first == 0) res_zeros += kv.second;
        res_mean += std::abs(kv.first) * kv.second;
        grad_count += kv.second;
    }
    if (grad_count > 0) res_mean /= grad_count;
    report.residual_dist_stats.mean_abs = res_mean;
    if (grad_count > 0) report.residual_dist_stats.zero_pct = (double)res_zeros / grad_count * 100.0;
    
    double res_var = 0;
    for (const auto& kv : glob_grad_counts) {
        double d = std::abs(kv.first) - res_mean;
        res_var += (d * d) * kv.second;
    }
    if (grad_count > 0) report.residual_dist_stats.variance = res_var / grad_count;
    
    if (grad_count > 0) {
        std::vector<std::pair<int32_t, std::size_t>> sorted_grad(glob_grad_counts.begin(), glob_grad_counts.end());
        std::sort(sorted_grad.begin(), sorted_grad.end(), [](const auto& a, const auto& b) {
            return std::abs(a.first) < std::abs(b.first);
        });
        std::size_t accum = 0;
        bool med_found = false, p95_found = false, p99_found = false;
        for (const auto& kv : sorted_grad) {
            accum += kv.second;
            if (!med_found && accum >= grad_count / 2) {
                report.residual_dist_stats.median = std::abs(kv.first);
                med_found = true;
            }
            if (!p95_found && accum >= (grad_count * 95) / 100) {
                report.residual_dist_stats.p95 = std::abs(kv.first);
                p95_found = true;
            }
            if (!p99_found && accum >= (grad_count * 99) / 100) {
                report.residual_dist_stats.p99 = std::abs(kv.first);
                p99_found = true;
            }
        }
        report.residual_dist_stats.max_val = std::abs(sorted_grad.back().first);
    }
    
    double max_res_corr = std::max({
        std::abs(report.correlation_stats.residual_horizontal),
        std::abs(report.correlation_stats.residual_vertical),
        std::abs(report.correlation_stats.residual_diagonal)
    });
    
    // Wavelets are only beneficial if there is remaining spatial structure (correlation > 0.3)
    // If correlation is near 0, the residuals are noise and DWT will just expand entropy.
    report.transform_eval_stats.decision_use_wavelet = (max_res_corr > 0.3);
    
    if (ll_count > 0) report.wavelet_stats.ll_count = ll_count;
    report.wavelet_stats.lh_count = lh_count;
    report.wavelet_stats.hl_count = hl_count;
    report.wavelet_stats.hh_count = hh_count;
    if (ll_count > 0) report.wavelet_stats.ll_zero_pct = (double)ll_zeros / ll_count * 100.0;
    if (lh_count > 0) report.wavelet_stats.lh_zero_pct = (double)lh_zeros / lh_count * 100.0;
    if (hl_count > 0) report.wavelet_stats.hl_zero_pct = (double)hl_zeros / hl_count * 100.0;
    if (hh_count > 0) report.wavelet_stats.hh_zero_pct = (double)hh_zeros / hh_count * 100.0;
    
    report.wavelet_stats.ll_entropy = calculate_entropy(ll_map, ll_count);
    report.wavelet_stats.lh_entropy = calculate_entropy(lh_map, lh_count);
    report.wavelet_stats.hl_entropy = calculate_entropy(hl_map, hl_count);
    report.wavelet_stats.hh_entropy = calculate_entropy(hh_map, hh_count);
    
    auto compute_subband_metrics = [](const std::unordered_map<int32_t, std::size_t>& counts, std::size_t count, double& mean, double& var, double& energy) {
        if (count == 0) return;
        double sum = 0, e_sum = 0;
        for (const auto& kv : counts) {
            double av = std::abs(kv.first);
            sum += av * kv.second;
            e_sum += (av * av) * kv.second;
        }
        mean = sum / count;
        energy = e_sum;
        double v_sum = 0;
        for (const auto& kv : counts) {
            double d = std::abs(kv.first) - mean;
            v_sum += (d * d) * kv.second;
        }
        var = v_sum / count;
    };
    
    double ll_e=0, lh_e=0, hl_e=0, hh_e=0;
    compute_subband_metrics(ll_map, ll_count, report.wavelet_stats.ll_mean_mag, report.wavelet_stats.ll_var, ll_e);
    compute_subband_metrics(lh_map, lh_count, report.wavelet_stats.lh_mean_mag, report.wavelet_stats.lh_var, lh_e);
    compute_subband_metrics(hl_map, hl_count, report.wavelet_stats.hl_mean_mag, report.wavelet_stats.hl_var, hl_e);
    compute_subband_metrics(hh_map, hh_count, report.wavelet_stats.hh_mean_mag, report.wavelet_stats.hh_var, hh_e);
    
    double total_energy = ll_e + lh_e + hl_e + hh_e;
    if (total_energy > 0) {
        report.wavelet_stats.ll_energy_pct = (ll_e / total_energy) * 100.0;
        report.wavelet_stats.lh_energy_pct = (lh_e / total_energy) * 100.0;
        report.wavelet_stats.hl_energy_pct = (hl_e / total_energy) * 100.0;
        report.wavelet_stats.hh_energy_pct = (hh_e / total_energy) * 100.0;
    }
    
    std::unordered_map<int32_t, std::size_t> all_coeffs_map;
    for (const auto& kv : ll_map) all_coeffs_map[kv.first] += kv.second;
    for (const auto& kv : lh_map) all_coeffs_map[kv.first] += kv.second;
    for (const auto& kv : hl_map) all_coeffs_map[kv.first] += kv.second;
    for (const auto& kv : hh_map) all_coeffs_map[kv.first] += kv.second;
    std::size_t total_wv_count = ll_count + lh_count + hl_count + hh_count;
    if (total_wv_count > 0) {
        std::vector<std::pair<int32_t, std::size_t>> sorted_wv(all_coeffs_map.begin(), all_coeffs_map.end());
        std::sort(sorted_wv.begin(), sorted_wv.end(), [](const auto& a, const auto& b) {
            return std::abs(a.first) < std::abs(b.first);
        });
        std::size_t accum = 0;
        bool p95_found = false, p99_found = false;
        for (const auto& kv : sorted_wv) {
            accum += kv.second;
            if (!p95_found && accum >= (total_wv_count * 95) / 100) {
                report.wavelet_stats.p95_coeff = std::abs(kv.first);
                p95_found = true;
            }
            if (!p99_found && accum >= (total_wv_count * 99) / 100) {
                report.wavelet_stats.p99_coeff = std::abs(kv.first);
                p99_found = true;
            }
        }
    }
    
    if (total_zero_runs > 0) {
        report.wavelet_stats.avg_zero_run = (double)total_zero_run_length / total_zero_runs;
    }
    report.wavelet_stats.max_zero_run = w_stats.max_zero_run;
    
    if (total_quad_leaves > 0) {
        report.context_stats.unique_contexts = total_contexts / total_quad_leaves;
        if (total_contexts > 0) {
            report.context_stats.avg_symbols_per_context = (double)total_symbols / total_contexts;
        }
        std::size_t min_c = std::numeric_limits<std::size_t>::max();
        std::size_t max_c = 0;
        std::vector<std::size_t> sizes;
        sizes.reserve(global_context_sizes.size());
        for (const auto& kv : global_context_sizes) {
            sizes.push_back(kv.second);
            min_c = std::min(min_c, kv.second);
            max_c = std::max(max_c, kv.second);
        }
        if (!sizes.empty()) {
            std::sort(sizes.begin(), sizes.end());
            report.context_stats.smallest_context = min_c;
            report.context_stats.largest_context = max_c;
            report.context_stats.median_context_size = sizes[sizes.size() / 2];
        }
    }
    
    report.transform_eval_stats.actual_wavelet_benefit = report.quadtree_entropy - report.dwt_quadtree_entropy;
    bool actual_benefit_positive = (report.transform_eval_stats.actual_wavelet_benefit > 0);
    report.transform_eval_stats.prediction_correct = (report.transform_eval_stats.decision_use_wavelet == actual_benefit_positive);
    
    // If the heuristic says YES (correlation > 0.3), we predict a gain of ~0.5 bpp.
    // If the heuristic says NO (correlation <= 0.3), we predict a penalty (gain of -0.5 bpp).
    if (report.transform_eval_stats.decision_use_wavelet) {
        report.transform_eval_stats.predicted_gain = 0.5;
    } else {
        report.transform_eval_stats.predicted_gain = -0.5;
    }
    
    report.transform_eval_stats.prediction_error = std::abs(report.transform_eval_stats.actual_wavelet_benefit - report.transform_eval_stats.predicted_gain);

    report.spatial.delta_x_entropy = report.global_predictors.left_entropy;
    report.spatial.delta_y_entropy = report.global_predictors.above_entropy;

    return report;
}

} // namespace xtm::analyzer
