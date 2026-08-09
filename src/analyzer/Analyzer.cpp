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
#include "xtm/coding/Pipeline.hpp"

namespace xtm::analyzer {

struct SuperblockStats {
    std::unordered_map<int32_t, std::size_t> glob_left, glob_grad, glob_jpegls, glob_polynomial, glob_gap, glob_least_squares;
    std::unordered_map<int32_t, std::size_t> block64_left, block64_grad, block64_jpegls, block64_polynomial, block64_gap, block64_least_squares;
    
    double total_adaptive_64_bits = 0;
    double quad_bits = 0;
    double dwt_bits = 0;
    double quad_overhead = 0;
    std::size_t quad_leaves = 0;
    
    QuadtreeStats q_stats;
    PredictorUsage p_usage;
    SubbandStats w_stats;
    
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
    
    void merge(const SuperblockStats& o) {
        auto merge_counts = [](std::unordered_map<int32_t, std::size_t>& dst, const std::unordered_map<int32_t, std::size_t>& src) {
            for (const auto& kv : src) dst[kv.first] += kv.second;
        };
        merge_counts(glob_left, o.glob_left); merge_counts(glob_grad, o.glob_grad); merge_counts(glob_jpegls, o.glob_jpegls); merge_counts(glob_polynomial, o.glob_polynomial);
        merge_counts(glob_gap, o.glob_gap); merge_counts(glob_least_squares, o.glob_least_squares);
        merge_counts(block64_left, o.block64_left); merge_counts(block64_grad, o.block64_grad); merge_counts(block64_jpegls, o.block64_jpegls); merge_counts(block64_polynomial, o.block64_polynomial);
        merge_counts(block64_gap, o.block64_gap); merge_counts(block64_least_squares, o.block64_least_squares);
        total_adaptive_64_bits += o.total_adaptive_64_bits;
        quad_bits += o.quad_bits; dwt_bits += o.dwt_bits; quad_overhead += o.quad_overhead;
        quad_leaves += o.quad_leaves;
        
        q_stats.size_512_count += o.q_stats.size_512_count; q_stats.size_256_count += o.q_stats.size_256_count;
        q_stats.size_128_count += o.q_stats.size_128_count; q_stats.size_64_count += o.q_stats.size_64_count;
        
        p_usage.left_count += o.p_usage.left_count; p_usage.left_mag_sum += o.p_usage.left_mag_sum; p_usage.left_final_bits += o.p_usage.left_final_bits; p_usage.left_final_pixels += o.p_usage.left_final_pixels;
        p_usage.gradient_count += o.p_usage.gradient_count; p_usage.gradient_mag_sum += o.p_usage.gradient_mag_sum; p_usage.gradient_final_bits += o.p_usage.gradient_final_bits; p_usage.gradient_final_pixels += o.p_usage.gradient_final_pixels;
        p_usage.jpegls_count += o.p_usage.jpegls_count; p_usage.jpegls_mag_sum += o.p_usage.jpegls_mag_sum; p_usage.jpegls_final_bits += o.p_usage.jpegls_final_bits; p_usage.jpegls_final_pixels += o.p_usage.jpegls_final_pixels;
        p_usage.polynomial_count += o.p_usage.polynomial_count; p_usage.polynomial_mag_sum += o.p_usage.polynomial_mag_sum; p_usage.polynomial_final_bits += o.p_usage.polynomial_final_bits; p_usage.polynomial_final_pixels += o.p_usage.polynomial_final_pixels;
        p_usage.gap_count += o.p_usage.gap_count; p_usage.gap_mag_sum += o.p_usage.gap_mag_sum; p_usage.gap_final_bits += o.p_usage.gap_final_bits; p_usage.gap_final_pixels += o.p_usage.gap_final_pixels;
        p_usage.least_squares_count += o.p_usage.least_squares_count; p_usage.least_squares_mag_sum += o.p_usage.least_squares_mag_sum; p_usage.least_squares_final_bits += o.p_usage.least_squares_final_bits; p_usage.least_squares_final_pixels += o.p_usage.least_squares_final_pixels;
        p_usage.second_order_pass_count += o.p_usage.second_order_pass_count;
        p_usage.second_order_bits_savings += o.p_usage.second_order_bits_savings;
        p_usage.base_bits_total += o.p_usage.base_bits_total;
        ll_zeros += o.ll_zeros; lh_zeros += o.lh_zeros; hl_zeros += o.hl_zeros; hh_zeros += o.hh_zeros;
        merge_counts(ll_map, o.ll_map); ll_count += o.ll_count;
        merge_counts(lh_map, o.lh_map); lh_count += o.lh_count;
        merge_counts(hl_map, o.hl_map); hl_count += o.hl_count;
        merge_counts(hh_map, o.hh_map); hh_count += o.hh_count;
        
        total_zero_runs += o.total_zero_runs; total_zero_run_length += o.total_zero_run_length;
        w_stats.max_zero_run = std::max(w_stats.max_zero_run, o.w_stats.max_zero_run);
        
        total_symbols += o.total_symbols; total_contexts += o.total_contexts;
        for (const auto& kv : o.global_context_sizes) global_context_sizes[kv.first] += kv.second;
        
        easy_count += o.easy_count; medium_count += o.medium_count; hard_count += o.hard_count;
        easy_entropy += o.easy_entropy; medium_entropy += o.medium_entropy; hard_entropy += o.hard_entropy;
        exact += o.exact; w1 += o.w1; w2 += o.w2; w5 += o.w5; w10 += o.w10; res_count += o.res_count;
        merge_counts(histogram, o.histogram);
    }
};

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

AnalysisReport analyze_terrain(const TerrainView& view, double scale, coding::ContextModel model, const AnalyzerOptions& options) {
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
    
    if (scale_factor > 1) {
        for (size_t i = 0; i < grid.data.size(); ++i) {
            if (!grid.nodata_mask.empty() && grid.nodata_mask[i]) continue;
            // Similar to Encoder.cpp: m = val / scale_factor
            grid.data[i] = grid.data[i] / scale_factor;
        }
    }
    
    compute_correlation(grid.data.data(), grid.width, grid.height, 
                        report.correlation_stats.raw_horizontal, 
                        report.correlation_stats.raw_vertical, 
                        report.correlation_stats.raw_diagonal);

    // Multithreaded analysis over 512x512 superblocks
    SuperblockStats global_stats;
    std::mutex stats_mutex;
    predictor::PredictorBank global_bank;
    PredictorSelector global_selector(global_bank.ordered(), options.enable_wavelet_analysis ? PipelineType::Wavelet : PipelineType::Predictor, 1.0, model);
    coding::Options pipeline_opts;
    pipeline_opts.pipeline_type = options.enable_wavelet_analysis ? PipelineType::Wavelet : PipelineType::Predictor;
    pipeline_opts.context_model = model;
    
    coding::run_pipeline(grid, pipeline_opts, global_selector, [&](const terrain::IntGrid& sgrid, uint32_t sx, uint32_t sy, std::vector<partition::QuadtreeNode>& quad_leaves, double q_bits, const PredictorSelector& selector, double /*partition_time_ms*/) {
        (void)sx; (void)sy;
        SuperblockStats l_stats;
        
        predictor::PredictorBank local_bank;
        
        partition::BlockView full_sb{&sgrid, 0, 0, sgrid.width, sgrid.height};
        
        auto accumulate = [](const std::vector<int32_t>& res, std::unordered_map<int32_t, std::size_t>& counts) {
            for (int32_t r : res) counts[r]++;
        };
        
        predictor::PredictionResult l_glob;
        local_bank.left.encode(full_sb, l_glob); accumulate(l_glob.residuals, l_stats.glob_left);
            predictor::PredictionResult g_glob;
            local_bank.gradient.encode(full_sb, g_glob); accumulate(g_glob.residuals, l_stats.glob_grad);
            predictor::PredictionResult j_glob;
            local_bank.jpegls.encode(full_sb, j_glob); accumulate(j_glob.residuals, l_stats.glob_jpegls);
            predictor::PredictionResult poly_glob;
            local_bank.polynomial.encode(full_sb, poly_glob); accumulate(poly_glob.residuals, l_stats.glob_polynomial);
            predictor::PredictionResult gp_glob;
            local_bank.gap.encode(full_sb, gp_glob); accumulate(gp_glob.residuals, l_stats.glob_gap);
            predictor::PredictionResult ls_glob;
            local_bank.least_squares.encode(full_sb, ls_glob); accumulate(ls_glob.residuals, l_stats.glob_least_squares);
            const terrain::IntGrid& const_sgrid = sgrid;
            auto blocks64 = partition::FixedGridPartitioner::partition(const_sgrid, 64);
            for (const auto& b : blocks64) {
                predictor::PredictionResult scratch;
                local_bank.left.encode(b, scratch); accumulate(scratch.residuals, l_stats.block64_left);
                local_bank.gradient.encode(b, scratch); accumulate(scratch.residuals, l_stats.block64_grad);
                local_bank.jpegls.encode(b, scratch); accumulate(scratch.residuals, l_stats.block64_jpegls);
                local_bank.polynomial.encode(b, scratch); accumulate(scratch.residuals, l_stats.block64_polynomial);
                local_bank.gap.encode(b, scratch); accumulate(scratch.residuals, l_stats.block64_gap);
                local_bank.least_squares.encode(b, scratch); accumulate(scratch.residuals, l_stats.block64_least_squares);
                auto res = selector.select(b);
                l_stats.total_adaptive_64_bits += res.total_bits;
                
                double block_entropy = calculate_entropy(res.best_encoded.residuals);
                if (block_entropy < 3.0) { l_stats.easy_count++; l_stats.easy_entropy += block_entropy; }
                else if (block_entropy < 7.0) { l_stats.medium_count++; l_stats.medium_entropy += block_entropy; }
                else { l_stats.hard_count++; l_stats.hard_entropy += block_entropy; }
                
            }
            
            l_stats.quad_bits += q_bits;
            l_stats.quad_leaves += quad_leaves.size();
            
            double sum_leaf_bits = 0.0;
            for (auto& leaf : quad_leaves) {
                sum_leaf_bits += leaf.selection.total_bits;
            }
            l_stats.quad_overhead += (q_bits - sum_leaf_bits);
            
            for (auto& leaf : quad_leaves) {
                double mag_sum = 0;
                for (int32_t r : leaf.selection.best_encoded.residuals) {
                    mag_sum += std::abs(r);
                    l_stats.histogram[r]++;
                    l_stats.res_count++;
                    int32_t abs_r = std::abs(r);
                    if (abs_r == 0) l_stats.exact++;
                    if (abs_r <= 1) l_stats.w1++;
                    if (abs_r <= 2) l_stats.w2++;
                    if (abs_r <= 5) l_stats.w5++;
                    if (abs_r <= 10) l_stats.w10++;
                }
                if (!leaf.selection.best_encoded.residuals.empty()) {
                    mag_sum /= leaf.selection.best_encoded.residuals.size();
                }
                
                double fb = leaf.selection.total_bits;
                std::size_t fp = static_cast<std::size_t>(leaf.block.width) * leaf.block.height;
                if (leaf.selection.best_predictor == &global_bank.left) { l_stats.p_usage.left_count++; l_stats.p_usage.left_mag_sum += mag_sum; l_stats.p_usage.left_final_bits += fb; l_stats.p_usage.left_final_pixels += fp; }
                else if (leaf.selection.best_predictor == &global_bank.gradient) { l_stats.p_usage.gradient_count++; l_stats.p_usage.gradient_mag_sum += mag_sum; l_stats.p_usage.gradient_final_bits += fb; l_stats.p_usage.gradient_final_pixels += fp; }
                else if (leaf.selection.best_predictor == &global_bank.jpegls) { l_stats.p_usage.jpegls_count++; l_stats.p_usage.jpegls_mag_sum += mag_sum; l_stats.p_usage.jpegls_final_bits += fb; l_stats.p_usage.jpegls_final_pixels += fp; }
                else if (leaf.selection.best_predictor == &global_bank.polynomial) { l_stats.p_usage.polynomial_count++; l_stats.p_usage.polynomial_mag_sum += mag_sum; l_stats.p_usage.polynomial_final_bits += fb; l_stats.p_usage.polynomial_final_pixels += fp; }
                else if (leaf.selection.best_predictor == &global_bank.gap) { l_stats.p_usage.gap_count++; l_stats.p_usage.gap_mag_sum += mag_sum; l_stats.p_usage.gap_final_bits += fb; l_stats.p_usage.gap_final_pixels += fp; }
                else if (leaf.selection.best_predictor == &global_bank.least_squares) { l_stats.p_usage.least_squares_count++; l_stats.p_usage.least_squares_mag_sum += mag_sum; l_stats.p_usage.least_squares_final_bits += fb; l_stats.p_usage.least_squares_final_pixels += fp; }
                
                if (leaf.selection.use_second_order) {
                    l_stats.p_usage.second_order_pass_count++;
                }
                l_stats.p_usage.second_order_bits_savings += leaf.selection.second_order_bits_savings;
                l_stats.p_usage.base_bits_total += leaf.selection.base_bits;
                
                uint32_t w = leaf.block.width;
                if (w == 512) l_stats.q_stats.size_512_count++;
                else if (w == 256) l_stats.q_stats.size_256_count++;
                else if (w == 128) l_stats.q_stats.size_128_count++;
                else l_stats.q_stats.size_64_count++;
                uint32_t max_levels = coding::max_wavelet_levels(leaf.block.width, leaf.block.height);
                
                std::vector<int32_t> data = leaf.selection.best_encoded.residuals;
                bool has_prec = (leaf.selection.best_prec_predictor != nullptr);
                if (data.empty()) {
                    size_t required_size = leaf.block.width * leaf.block.height * (has_prec ? 2 : 1);
                    data.resize(required_size, 0);
                }
                
                if (options.enable_wavelet_analysis) {
                    std::vector<int32_t> wv_data = data;
                    
                    if (max_levels > 0) {
                        uint32_t ll_w = leaf.block.width >> 1;
                        uint32_t ll_h = leaf.block.height >> 1;
                        for (uint32_t y = 0; y < leaf.block.height; ++y) {
                            for (uint32_t x = 0; x < leaf.block.width; ++x) {
                                int32_t val = wv_data[y * leaf.block.width + x];
                                bool is_zero = (val == 0);
                                if (x < ll_w && y < ll_h) {
                                    l_stats.ll_map[val]++; l_stats.ll_count++; if (is_zero) l_stats.ll_zeros++;
                                } else if (x >= ll_w && y < ll_h) {
                                    l_stats.hl_map[val]++; l_stats.hl_count++; if (is_zero) l_stats.hl_zeros++;
                                } else if (x < ll_w && y >= ll_h) {
                                    l_stats.lh_map[val]++; l_stats.lh_count++; if (is_zero) l_stats.lh_zeros++;
                                } else {
                                    l_stats.hh_map[val]++; l_stats.hh_count++; if (is_zero) l_stats.hh_zeros++;
                                }
                            }
                        }
                    }
                    
                    std::vector<int32_t> wv_mag_classes;
                    std::vector<int32_t> wv_run_lengths;
                    std::unordered_map<coding::Context, uint32_t> wv_context_sizes;
                    uint32_t wv_remainder_bits_int = 0;
                    coding::analyze_symbols(wv_data, leaf.block.width, leaf.block.height, model, false, wv_mag_classes, wv_run_lengths, wv_context_sizes, wv_remainder_bits_int);
                    
                    double wv_mag_class_bits = calculate_entropy(wv_mag_classes) * wv_mag_classes.size();
                    double wv_run_bits = wv_run_lengths.empty() ? 0 : calculate_entropy(wv_run_lengths) * wv_run_lengths.size();
                    double wv_residual = wv_mag_class_bits + wv_remainder_bits_int + wv_run_bits;
                    
                    double c_id = 8.0;
                    double c_params = leaf.selection.best_encoded.parameters.size() * 32.0;
                    l_stats.dwt_bits += c_id + c_params + wv_residual;
                }
                
                std::vector<int32_t> mag_classes;
                std::vector<int32_t> run_lengths;
                std::unordered_map<coding::Context, uint32_t> context_sizes;
                uint32_t remainder_bits_int = 0;
                
                coding::analyze_symbols(data, leaf.block.width, leaf.block.height, model, has_prec, mag_classes, run_lengths, context_sizes, remainder_bits_int);
                
                std::unordered_set<coding::Context> block_contexts;
                for (const auto& kv : context_sizes) {
                    block_contexts.insert(kv.first);
                    l_stats.global_context_sizes[kv.first] += kv.second;
                }
                
                l_stats.total_symbols += mag_classes.size();
                
                for (uint32_t run : run_lengths) {
                    l_stats.total_zero_runs++;
                    l_stats.total_zero_run_length += run;
                    l_stats.w_stats.max_zero_run = std::max(l_stats.w_stats.max_zero_run, static_cast<std::size_t>(run));
                }
                
                l_stats.total_contexts += block_contexts.size();
            }
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        global_stats.merge(l_stats);
    });
    
    report.quadtree_leaves = global_stats.quad_leaves;
    if (global_stats.res_count > 0) {
        report.predictor_confidence.pct_exact = (double)global_stats.exact / global_stats.res_count * 100.0;
        report.predictor_confidence.pct_within_1 = (double)global_stats.w1 / global_stats.res_count * 100.0;
        report.predictor_confidence.pct_within_2 = (double)global_stats.w2 / global_stats.res_count * 100.0;
        report.predictor_confidence.pct_within_5 = (double)global_stats.w5 / global_stats.res_count * 100.0;
        report.predictor_confidence.pct_within_10 = (double)global_stats.w10 / global_stats.res_count * 100.0;
    }
    std::size_t total_difficulty = global_stats.easy_count + global_stats.medium_count + global_stats.hard_count;
    if (total_difficulty > 0) {
        report.prediction_difficulty.easy_pct = (double)global_stats.easy_count / total_difficulty * 100.0;
        report.prediction_difficulty.medium_pct = (double)global_stats.medium_count / total_difficulty * 100.0;
        report.prediction_difficulty.hard_pct = (double)global_stats.hard_count / total_difficulty * 100.0;
        if (global_stats.easy_count > 0) report.prediction_difficulty.easy_avg_entropy = global_stats.easy_entropy / global_stats.easy_count;
        if (global_stats.medium_count > 0) report.prediction_difficulty.medium_avg_entropy = global_stats.medium_entropy / global_stats.medium_count;
        if (global_stats.hard_count > 0) report.prediction_difficulty.hard_avg_entropy = global_stats.hard_entropy / global_stats.hard_count;
    }
    for (const auto& kv : global_stats.histogram) {
        report.residual_histogram.push_back({kv.first, (double)kv.second / global_stats.res_count * 100.0});
    }
    std::sort(report.residual_histogram.begin(), report.residual_histogram.end());
    report.quadtree_stats = global_stats.q_stats;
    report.predictor_usage = global_stats.p_usage;
    
    report.predictor_usage.left_count = global_stats.p_usage.left_count;
    report.predictor_usage.gradient_count = global_stats.p_usage.gradient_count;
    report.predictor_usage.jpegls_count = global_stats.p_usage.jpegls_count;
    report.predictor_usage.polynomial_count = global_stats.p_usage.polynomial_count;
    report.predictor_usage.gap_count = global_stats.p_usage.gap_count;
    report.predictor_usage.least_squares_count = global_stats.p_usage.least_squares_count;
    report.predictor_usage.second_order_pass_count = global_stats.p_usage.second_order_pass_count;
    
    if (global_stats.p_usage.base_bits_total > 0) {
        report.predictor_usage.second_order_bits_savings_pct = (global_stats.p_usage.second_order_bits_savings / global_stats.p_usage.base_bits_total) * 100.0;
    }
    
    report.predictor_usage.left_mag_sum = global_stats.p_usage.left_mag_sum;
    report.predictor_usage.gradient_mag_sum = global_stats.p_usage.gradient_mag_sum;
    report.predictor_usage.jpegls_mag_sum = global_stats.p_usage.jpegls_mag_sum;
    report.predictor_usage.polynomial_mag_sum = global_stats.p_usage.polynomial_mag_sum;
    report.predictor_usage.gap_mag_sum = global_stats.p_usage.gap_mag_sum;
    report.predictor_usage.least_squares_mag_sum = global_stats.p_usage.least_squares_mag_sum;

    report.predictor_usage.left_final_bits = global_stats.p_usage.left_final_bits;
    report.predictor_usage.gradient_final_bits = global_stats.p_usage.gradient_final_bits;
    report.predictor_usage.jpegls_final_bits = global_stats.p_usage.jpegls_final_bits;
    report.predictor_usage.polynomial_final_bits = global_stats.p_usage.polynomial_final_bits;
    report.predictor_usage.gap_final_bits = global_stats.p_usage.gap_final_bits;
    report.predictor_usage.least_squares_final_bits = global_stats.p_usage.least_squares_final_bits;

    report.predictor_usage.left_final_pixels = global_stats.p_usage.left_final_pixels;
    report.predictor_usage.gradient_final_pixels = global_stats.p_usage.gradient_final_pixels;
    report.predictor_usage.jpegls_final_pixels = global_stats.p_usage.jpegls_final_pixels;
    report.predictor_usage.polynomial_final_pixels = global_stats.p_usage.polynomial_final_pixels;
    report.predictor_usage.gap_final_pixels = global_stats.p_usage.gap_final_pixels;
    report.predictor_usage.least_squares_final_pixels = global_stats.p_usage.least_squares_final_pixels;

    std::size_t grid_samples = report.sample_count;
    report.global_predictors.left_entropy = calculate_entropy(global_stats.glob_left, grid_samples);
    report.global_predictors.gradient_entropy = calculate_entropy(global_stats.glob_grad, grid_samples);
    report.global_predictors.jpegls_entropy = calculate_entropy(global_stats.glob_jpegls, grid_samples);
    report.global_predictors.polynomial_entropy = calculate_entropy(global_stats.glob_polynomial, grid_samples);
    report.global_predictors.gap_entropy = calculate_entropy(global_stats.glob_gap, grid_samples);
    report.global_predictors.least_squares_entropy = calculate_entropy(global_stats.glob_least_squares, grid_samples);
    report.block64_predictors.left_entropy = calculate_entropy(global_stats.block64_left, grid_samples);
    report.block64_predictors.gradient_entropy = calculate_entropy(global_stats.block64_grad, grid_samples);
    report.block64_predictors.jpegls_entropy = calculate_entropy(global_stats.block64_jpegls, grid_samples);
    report.block64_predictors.polynomial_entropy = calculate_entropy(global_stats.block64_polynomial, grid_samples);
    report.block64_predictors.gap_entropy = calculate_entropy(global_stats.block64_gap, grid_samples);
    report.block64_predictors.least_squares_entropy = calculate_entropy(global_stats.block64_least_squares, grid_samples);
    report.adaptive_block64_entropy = global_stats.total_adaptive_64_bits / grid_samples;
    report.quadtree_entropy = global_stats.quad_bits / grid_samples;
    report.dwt_quadtree_entropy = (global_stats.dwt_bits + global_stats.quad_overhead) / grid_samples;
    
    {
        predictor::PredictorBank temp_bank;
        predictor::PredictionResult temp_res;

        temp_bank.gradient.encode(partition::BlockView{&grid, 0, 0, grid.width, grid.height}, temp_res);
        compute_correlation(temp_res.residuals.data(), grid.width, grid.height, 
                            report.correlation_stats.residual_horizontal, 
                            report.correlation_stats.residual_vertical, 
                            report.correlation_stats.residual_diagonal);
    }
                        
    double res_mean = 0;
    size_t res_zeros = 0;
    std::size_t grad_count = 0;
    for (const auto& kv : global_stats.glob_grad) {
        if (kv.first == 0) res_zeros += kv.second;
        res_mean += std::abs(kv.first) * kv.second;
        grad_count += kv.second;
    }
    if (grad_count > 0) res_mean /= grad_count;
    report.residual_dist_stats.mean_abs = res_mean;
    if (grad_count > 0) report.residual_dist_stats.zero_pct = (double)res_zeros / grad_count * 100.0;
    
    double res_var = 0;
    for (const auto& kv : global_stats.glob_grad) {
        double d = std::abs(kv.first) - res_mean;
        res_var += (d * d) * kv.second;
    }
    if (grad_count > 0) report.residual_dist_stats.variance = res_var / grad_count;
    
    if (grad_count > 0) {
        std::vector<std::pair<int32_t, std::size_t>> sorted_grad(global_stats.glob_grad.begin(), global_stats.glob_grad.end());
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
    
    report.transform_eval_stats.decision_use_wavelet = (max_res_corr > 0.4 && report.adaptive_block64_entropy > 3.0);
    
    if (options.enable_wavelet_analysis) {
        if (global_stats.ll_count > 0) report.wavelet_stats.ll_count = global_stats.ll_count;
        report.wavelet_stats.lh_count = global_stats.lh_count;
        report.wavelet_stats.hl_count = global_stats.hl_count;
        report.wavelet_stats.hh_count = global_stats.hh_count;
        if (global_stats.ll_count > 0) report.wavelet_stats.ll_zero_pct = (double)global_stats.ll_zeros / global_stats.ll_count * 100.0;
        if (global_stats.lh_count > 0) report.wavelet_stats.lh_zero_pct = (double)global_stats.lh_zeros / global_stats.lh_count * 100.0;
        if (global_stats.hl_count > 0) report.wavelet_stats.hl_zero_pct = (double)global_stats.hl_zeros / global_stats.hl_count * 100.0;
        if (global_stats.hh_count > 0) report.wavelet_stats.hh_zero_pct = (double)global_stats.hh_zeros / global_stats.hh_count * 100.0;
    
    report.wavelet_stats.ll_entropy = calculate_entropy(global_stats.ll_map, global_stats.ll_count);
    report.wavelet_stats.lh_entropy = calculate_entropy(global_stats.lh_map, global_stats.lh_count);
    report.wavelet_stats.hl_entropy = calculate_entropy(global_stats.hl_map, global_stats.hl_count);
    report.wavelet_stats.hh_entropy = calculate_entropy(global_stats.hh_map, global_stats.hh_count);
    
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
    compute_subband_metrics(global_stats.ll_map, global_stats.ll_count, report.wavelet_stats.ll_mean_mag, report.wavelet_stats.ll_var, ll_e);
    compute_subband_metrics(global_stats.lh_map, global_stats.lh_count, report.wavelet_stats.lh_mean_mag, report.wavelet_stats.lh_var, lh_e);
    compute_subband_metrics(global_stats.hl_map, global_stats.hl_count, report.wavelet_stats.hl_mean_mag, report.wavelet_stats.hl_var, hl_e);
    compute_subband_metrics(global_stats.hh_map, global_stats.hh_count, report.wavelet_stats.hh_mean_mag, report.wavelet_stats.hh_var, hh_e);
    
    double total_energy = ll_e + lh_e + hl_e + hh_e;
    if (total_energy > 0) {
        report.wavelet_stats.ll_energy_pct = (ll_e / total_energy) * 100.0;
        report.wavelet_stats.lh_energy_pct = (lh_e / total_energy) * 100.0;
        report.wavelet_stats.hl_energy_pct = (hl_e / total_energy) * 100.0;
        report.wavelet_stats.hh_energy_pct = (hh_e / total_energy) * 100.0;
    }
    
    std::unordered_map<int32_t, std::size_t> all_coeffs_map;
    for (const auto& kv : global_stats.ll_map) all_coeffs_map[kv.first] += kv.second;
    for (const auto& kv : global_stats.lh_map) all_coeffs_map[kv.first] += kv.second;
    for (const auto& kv : global_stats.hl_map) all_coeffs_map[kv.first] += kv.second;
    for (const auto& kv : global_stats.hh_map) all_coeffs_map[kv.first] += kv.second;
    std::size_t total_wv_count = global_stats.ll_count + global_stats.lh_count + global_stats.hl_count + global_stats.hh_count;
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
    
    report.transform_eval_stats.actual_wavelet_benefit = report.quadtree_entropy - report.dwt_quadtree_entropy;
    bool actual_benefit_positive = (report.transform_eval_stats.actual_wavelet_benefit > 0);
    report.transform_eval_stats.prediction_correct = (report.transform_eval_stats.decision_use_wavelet == actual_benefit_positive);
    
    if (report.transform_eval_stats.decision_use_wavelet) {
        report.transform_eval_stats.predicted_gain = max_res_corr * 0.5;
    } else {
        report.transform_eval_stats.predicted_gain = -0.5;
    }
    
    report.transform_eval_stats.prediction_error = std::abs(report.transform_eval_stats.actual_wavelet_benefit - report.transform_eval_stats.predicted_gain);
    }
    
    if (global_stats.total_zero_runs > 0) {
        report.wavelet_stats.avg_zero_run = (double)global_stats.total_zero_run_length / global_stats.total_zero_runs;
    }
    report.wavelet_stats.max_zero_run = global_stats.w_stats.max_zero_run;
    
    if (global_stats.quad_leaves > 0) {
        report.context_stats.unique_contexts = global_stats.total_contexts / global_stats.quad_leaves;
        if (global_stats.total_contexts > 0) {
            report.context_stats.avg_symbols_per_context = (double)global_stats.total_symbols / global_stats.total_contexts;
        }
        std::size_t min_c = std::numeric_limits<std::size_t>::max();
        std::size_t max_c = 0;
        std::vector<std::size_t> sizes;
        sizes.reserve(global_stats.global_context_sizes.size());
        for (const auto& kv : global_stats.global_context_sizes) {
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

    report.spatial.delta_x_entropy = report.global_predictors.left_entropy;
    report.spatial.delta_y_entropy = 0; // Above predictor is removed, so no delta y entropy is directly available

    return report;
}

} // namespace xtm::analyzer
