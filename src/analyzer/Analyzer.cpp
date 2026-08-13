#include "xtm/analyzer/Analyzer.hpp"
#include "xtm/analyzer/Selector.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/Pipeline.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <limits>
#include <vector>

namespace xtm::analyzer {

namespace {

using clock = std::chrono::high_resolution_clock;

double elapsed_ms(clock::time_point from, clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// True Shannon entropy (bits/sample) of a residual stream. The histogram keys
// are sorted before summing, keeping the result deterministic across runs.
double shannon_bits_of(const std::vector<int32_t>& residuals,
                       std::unordered_map<int32_t, std::size_t>& counts) {
    counts.clear();
    for (int32_t r : residuals) counts[r]++;

    std::vector<std::pair<int32_t, std::size_t>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end());

    double total = static_cast<double>(residuals.size());
    if (total == 0.0) return 0.0;

    double bits = 0.0;
    for (const auto& kv : sorted) {
        double p = static_cast<double>(kv.second) / total;
        bits -= p * std::log2(p);
    }
    return bits;
}

void compute_correlation(const int32_t* data, std::uint32_t width, std::uint32_t height,
                         double& horiz, double& vert, double& diag) {
    if (width < 2 || height < 2) return;

    double mean = 0;
    std::size_t count = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0; i < count; ++i) mean += data[i];
    mean /= count;

    double var_sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        double d = data[i] - mean;
        var_sum += d * d;
    }

    if (var_sum == 0) {
        horiz = vert = diag = 1.0;
        return;
    }

    double cov_h = 0, cov_v = 0, cov_d = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
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

// Aggregation slot for one superblock (indexed by s_idx, written by exactly
// one handler invocation, then reduced serially in index order).
struct SuperblockStats {
    std::size_t blocks = 0;
    std::size_t sec_blocks = 0;
    std::size_t resid_pool[7] = {0, 0, 0, 0, 0, 0, 0};
    double resid_savings = 0.0;
    double quad_bits = 0.0;         // partitioner total (includes leaf selection costs)
    double structure_bits = 0.0;    // quad_bits minus the leaf selection costs

    double mag_bits = 0.0;
    double run_bits = 0.0;
    double rem_bits = 0.0;
    double param_bits = 0.0;
    double winner_abs_sum = 0.0;
    double winner_est_bits = 0.0;
    double wv_est_bits = 0.0;

    double leaf_cost_bits = 0.0;    // sum of per-leaf selection total_bits
    double leaf_cost_sq = 0.0;      // sum of per-leaf selection total_bits^2

    double quad_ms = 0.0;           // per-phase cumulative timings (parallel
    double eval_ms = 0.0;           // workers overlap, so the sums may exceed
    double entropy_ms = 0.0;        // the wall-clock total)

    std::size_t usage[6] = {0, 0, 0, 0, 0, 0};
    std::size_t leaf512 = 0;
    std::size_t leaf256 = 0;
    std::size_t leaf128 = 0;
    std::size_t leaf64 = 0;

    double sel_bits[6] = {0, 0, 0, 0, 0, 0};
    double shannon_bits[6] = {0, 0, 0, 0, 0, 0};
    double abs_sum[6] = {0, 0, 0, 0, 0, 0};
    std::size_t px[6] = {0, 0, 0, 0, 0, 0};
};

std::size_t predictor_index(const predictor::Predictor* p) {
    return p ? static_cast<std::size_t>(p->id()) : 0;
}

// Lightweight quadtree + selection pass over a grid, collecting only the
// aggregate cost. Deterministic (per-superblock slots reduced in order).
struct PassResult {
    double leaf_cost_bits = 0.0;
    double structure_bits = 0.0;
    std::size_t blocks = 0;
};

PassResult run_selection_pass(const terrain::IntGrid& grid, const coding::PipelineContext& ctx) {
    predictor::PredictorBank bank;
    PredictorSelector selector(bank.ordered(), ctx);

    struct Slot {
        double leaf_cost = 0.0;
        double structure = 0.0;
        std::size_t blocks = 0;
    };
    const std::uint32_t sb_size = 512;
    const std::uint32_t grid_sb_x = (grid.width + sb_size - 1) / sb_size;
    const std::uint32_t grid_sb_y = (grid.height + sb_size - 1) / sb_size;
    std::vector<Slot> slots(static_cast<std::size_t>(grid_sb_x) * grid_sb_y);

    coding::for_each_superblock(grid, ctx, selector,
        [&](const terrain::IntGrid& /*sgrid*/, std::uint32_t /*sx*/, std::uint32_t /*sy*/,
            std::uint32_t s_idx, std::vector<partition::QuadtreeNode>& leaves, double quad_bits,
            const analyzer::PredictorSelector& /*selector*/, double /*partition_time_ms*/) {
            Slot& slot = slots[s_idx];
            slot.blocks = leaves.size();
            for (const auto& leaf : leaves) {
                slot.leaf_cost += leaf.selection.total_bits;
            }
            slot.structure += quad_bits - slot.leaf_cost;
        });

    PassResult r;
    for (const Slot& slot : slots) {
        r.leaf_cost_bits += slot.leaf_cost;
        r.structure_bits += slot.structure;
        r.blocks += slot.blocks;
    }
    return r;
}

} // namespace

AnalysisReport analyze_terrain(const terrain::IntGrid& grid,
                               const RawElevationStats& raw,
                               const coding::PipelineContext& ctx,
                               const AnalyzerOptions& options) {
    AnalysisReport report;
    report.width = grid.width;
    report.height = grid.height;
    report.sample_count = static_cast<std::size_t>(grid.width) * grid.height;
    report.precision = ctx.precision;
    report.raw = raw;

    const bool has_mask = !grid.nodata_mask.empty();

    // ---- Quantized elevation stats + nodata count (single pass) ----
    {
        double sum = 0.0, sumsq = 0.0;
        std::size_t valid = 0;
        std::int32_t min_v = std::numeric_limits<int32_t>::max();
        std::int32_t max_v = std::numeric_limits<int32_t>::lowest();
        for (std::size_t i = 0; i < grid.data.size(); ++i) {
            if (has_mask && grid.nodata_mask[i]) {
                report.nodata_pixels++;
                continue;
            }
            int32_t v = grid.data[i];
            sum += v;
            sumsq += static_cast<double>(v) * v;
            if (v < min_v) min_v = v;
            if (v > max_v) max_v = v;
            valid++;
        }
        if (valid > 0) {
            report.quantized.min_val = min_v;
            report.quantized.max_val = max_v;
            report.quantized.mean = sum / static_cast<double>(valid);
            double variance = sumsq / static_cast<double>(valid) - report.quantized.mean * report.quantized.mean;
            report.quantized.stddev = (variance > 0.0) ? std::sqrt(variance) : 0.0;
        }
    }

    compute_correlation(grid.data.data(), grid.width, grid.height,
                        report.corr_h, report.corr_v, report.corr_d);

    // ---- Digit-plane entropies (precision guidance) ----
    {
        int mult = ctx.has_precision ? ctx.precision_multiplier : 1;
        int planes = 0;
        while (mult > 0 && mult % 10 == 0) {
            planes++;
            mult /= 10;
        }
        if (mult == 1) {
            const int num_planes = planes + 1;
            std::vector<std::array<std::uint64_t, 10>> bins(
                static_cast<std::size_t>(num_planes));
            for (std::size_t i = 0; i < grid.data.size(); ++i) {
                if (has_mask && grid.nodata_mask[i]) continue;
                std::uint32_t tmp = xtm::coding::safe_abs(grid.data[i]);
                for (int k = 0; k < num_planes; ++k) {
                    bins[static_cast<std::size_t>(k)][tmp % 10]++;
                    tmp /= 10;
                }
            }
            std::size_t total = report.sample_count - report.nodata_pixels;
            for (int k = 0; k < num_planes; ++k) {
                double bits = 0.0;
                for (int d = 0; d < 10; ++d) {
                    if (bins[static_cast<std::size_t>(k)][static_cast<std::size_t>(d)] > 0) {
                        double p = static_cast<double>(bins[static_cast<std::size_t>(k)][static_cast<std::size_t>(d)]) / total;
                        bits -= p * std::log2(p);
                    }
                }
                report.digit_planes.push_back({k, bits});
            }
        }
    }

    // ---- Shared prediction pass (same partitioner + selector as the encoder) ----
    predictor::PredictorBank bank;
    std::vector<const predictor::Predictor*> predictors_list = bank.ordered();
    PredictorSelector global_selector(predictors_list, ctx);

    // Per-plane estimate context: whole-superblock and wavelet coefficient
    // streams are never split, unlike the concatenated winner residuals.
    coding::PipelineContext plane_ctx = ctx;
    plane_ctx.has_precision = false;

    const std::uint32_t sb_size = 512;
    const std::uint32_t grid_sb_x = (grid.width + sb_size - 1) / sb_size;
    const std::uint32_t grid_sb_y = (grid.height + sb_size - 1) / sb_size;
    std::vector<SuperblockStats> slots(static_cast<std::size_t>(grid_sb_x) * grid_sb_y);

    coding::for_each_superblock(grid, ctx, global_selector,
        [&](const terrain::IntGrid& sgrid, std::uint32_t /*sx*/, std::uint32_t /*sy*/,
            std::uint32_t s_idx, std::vector<partition::QuadtreeNode>& leaves, double quad_bits,
            const analyzer::PredictorSelector& /*selector*/, double partition_time_ms) {
            SuperblockStats& s = slots[s_idx];
            s.quad_bits = quad_bits;
            s.blocks = leaves.size();
            s.quad_ms += partition_time_ms;

            double leaf_cost_sum = 0.0;
            const auto t_entropy0 = clock::now();
            for (const auto& leaf : leaves) {
                const SelectionResult& sel = leaf.selection;
                leaf_cost_sum += sel.total_bits;
                s.leaf_cost_bits += sel.total_bits;
                s.leaf_cost_sq += sel.total_bits * sel.total_bits;
                if (sel.best_predictor) {
                    s.usage[predictor_index(sel.best_predictor)]++;
                    if (sel.use_second_order) {
                        s.sec_blocks++;
                        s.resid_pool[static_cast<std::size_t>(sel.residual_predictor_id)]++;
                        s.resid_savings += sel.second_order_bits_savings;
                    }
                }

                double mag = 0.0, run = 0.0, rem = 0.0;
                estimate_shannon_bits(sel.best_residuals, &ctx, leaf.block.width, leaf.block.height, &mag, &run, &rem);
                s.mag_bits += mag;
                s.run_bits += run;
                s.rem_bits += rem;
                s.param_bits += static_cast<double>(sel.best_parameters.size()) * 32.0;
                s.winner_est_bits += mag + run + rem;
                for (int32_t r : sel.best_residuals) s.winner_abs_sum += xtm::coding::safe_abs(r);

                switch (leaf.block.width) {
                    case 512: s.leaf512++; break;
                    case 256: s.leaf256++; break;
                    case 128: s.leaf128++; break;
                    default: s.leaf64++; break;
                }

                if (options.enable_wavelet_analysis) {
                    std::uint32_t w = leaf.block.width;
                    std::uint32_t h = leaf.block.height;
                    std::vector<int32_t> wv(static_cast<std::size_t>(w) * h);
                    for (std::uint32_t y = 0; y < h; ++y) {
                        for (std::uint32_t x = 0; x < w; ++x) {
                            wv[y * w + x] = leaf.block.get(x, y);
                        }
                    }
                    std::uint32_t levels = coding::max_wavelet_levels(w, h);
                    if (levels > 0) {
                        transform::CDF53Transform::forward_2d(wv, w, h, levels);
                    }
                    s.wv_est_bits += estimate_shannon_bits(wv, &plane_ctx, w, h);
                }
            }
            s.structure_bits = quad_bits - leaf_cost_sum;
            s.entropy_ms += elapsed_ms(t_entropy0, clock::now());

            // Whole-superblock runs: what each predictor would cost alone.
            const auto t_eval0 = clock::now();
            partition::BlockView full_sb{&sgrid, 0, 0, sgrid.width, sgrid.height};
            std::vector<int32_t> scratch_res;
            std::vector<int32_t> scratch_param;
            std::unordered_map<int32_t, std::size_t> entropy_scratch;
            for (std::size_t i = 0; i < predictors_list.size(); ++i) {
                scratch_res.clear();
                scratch_param.clear();
                predictors_list[i]->encode(full_sb, scratch_res, scratch_param);
                s.sel_bits[i] += 8.0 + static_cast<double>(scratch_param.size()) * 32.0
                               + estimate_shannon_bits(scratch_res, &plane_ctx, sgrid.width, sgrid.height);
                s.shannon_bits[i] += shannon_bits_of(scratch_res, entropy_scratch)
                                   * static_cast<double>(scratch_res.size());
                for (int32_t r : scratch_res) s.abs_sum[i] += xtm::coding::safe_abs(r);
                s.px[i] += scratch_res.size();
            }
            s.eval_ms += elapsed_ms(t_eval0, clock::now());
        });

    // ---- Deterministic serial reduction in superblock order ----
    // The budget derives estimate + params from the leaf residuals directly,
    // so per-block header bits must be added here: predictor id (8) + prec
    // predictor id (8, when split precision) + parameter-count byte (8) +
    // nodata flag (1).
    const double inv_n = 1.0 / static_cast<double>(report.sample_count);
    const double per_block_overhead = 17.0 + (ctx.has_precision ? 8.0 : 0.0);

    for (const SuperblockStats& s : slots) {
        report.budget.magnitude_class_bpp += s.mag_bits;
        report.budget.zero_run_bpp += s.run_bits;
        report.budget.remainder_bpp += s.rem_bits;
        report.budget.params_bpp += s.param_bits;
        report.budget.overhead_bpp += s.structure_bits + s.blocks * per_block_overhead;
        report.predictor_estimate_bpp += s.winner_est_bits;
        report.wavelet_estimate_bpp += s.wv_est_bits;

        report.leaves_512 += s.leaf512;
        report.leaves_256 += s.leaf256;
        report.leaves_128 += s.leaf128;
        report.leaves_64 += s.leaf64;
        report.total_blocks += s.blocks;
        report.second_order_usage_pct += static_cast<double>(s.sec_blocks);
        report.residual_pool_savings_bits += s.resid_savings;
        report.time_quadtree_ms += s.quad_ms;
        report.time_predictor_eval_ms += s.eval_ms;
        report.time_entropy_ms += s.entropy_ms;
        for (std::size_t i = 0; i < 7; ++i) {
            report.residual_predictor_blocks[i] += s.resid_pool[i];
        }
    }

    report.budget.magnitude_class_bpp *= inv_n;
    report.budget.zero_run_bpp *= inv_n;
    report.budget.remainder_bpp *= inv_n;
    report.budget.params_bpp *= inv_n;
    report.budget.overhead_bpp *= inv_n;
    report.budget.total_bpp = report.budget.magnitude_class_bpp + report.budget.zero_run_bpp
                            + report.budget.remainder_bpp + report.budget.params_bpp
                            + report.budget.overhead_bpp;
    report.predictor_estimate_bpp *= inv_n;
    report.wavelet_estimate_bpp *= inv_n;
    report.second_order_usage_pct = report.total_blocks > 0
        ? 100.0 * report.second_order_usage_pct / static_cast<double>(report.total_blocks)
        : 0.0;
    report.second_order_savings_bpp = report.residual_pool_savings_bits * inv_n;

    const std::size_t block_index_bytes = report.total_blocks * 36u;
    report.estimated_file_bytes = report.budget.total_bpp * report.sample_count / 8.0
                                + block_index_bytes + 256.0;
    report.estimated_compression_ratio = report.estimated_file_bytes > 0
        ? static_cast<double>(report.sample_count) * 4.0 / report.estimated_file_bytes
        : 0.0;

    // Spread of the size estimate: per-leaf selection bits treated as
    // independent draws, propagated to payload bytes.
    {
        double bits_x = 0.0, bits_x2 = 0.0;
        std::size_t n = 0;
        for (const SuperblockStats& s : slots) {
            bits_x += s.leaf_cost_bits;
            bits_x2 += s.leaf_cost_sq;
            n += s.blocks;
        }
        if (n > 1) {
            double var = bits_x2 / static_cast<double>(n)
                       - (bits_x / static_cast<double>(n)) * (bits_x / static_cast<double>(n));
            report.estimated_bytes_stddev =
                (var > 0.0) ? std::sqrt(var * static_cast<double>(n)) / 8.0 : 0.0;
        }
    }

    // ---- Predictor table ----
    for (std::size_t i = 0; i < predictors_list.size(); ++i) {
        PredictorPerformance row;
        row.id = predictors_list[i]->id();
        row.name = predictors_list[i]->name();
        double sel_sum = 0.0, shan_sum = 0.0, abs_sum = 0.0;
        std::size_t px = 0;
        for (const SuperblockStats& slot : slots) {
            sel_sum += slot.sel_bits[i];
            shan_sum += slot.shannon_bits[i];
            abs_sum += slot.abs_sum[i];
            px += slot.px[i];
        }
        if (px > 0) {
            row.selection_bpp = sel_sum / static_cast<double>(px);
            row.shannon_bpp = shan_sum / static_cast<double>(px);
            row.avg_abs_residual = abs_sum / static_cast<double>(px);
        }
        row.usage_blocks = 0;
        for (const SuperblockStats& slot : slots) row.usage_blocks += slot.usage[i];
        report.predictors.push_back(std::move(row));
    }

    std::sort(report.predictors.begin(), report.predictors.end(),
              [](const PredictorPerformance& a, const PredictorPerformance& b) {
                  return a.selection_bpp < b.selection_bpp;
              });

    std::size_t best_usage = 0;
    for (const auto& row : report.predictors) {
        if (row.usage_blocks > best_usage) {
            best_usage = row.usage_blocks;
            report.chosen_predictor = row.id;
        }
    }
    report.chosen_usage_pct = report.total_blocks > 0
        ? 100.0 * static_cast<double>(best_usage) / static_cast<double>(report.total_blocks)
        : 0.0;

    // ---- Wavelet evaluation ----
    report.wavelet_evaluated = options.enable_wavelet_analysis;
    if (report.wavelet_evaluated) {
        report.wavelet_recommended = report.wavelet_estimate_bpp < report.predictor_estimate_bpp;
    }

    // ---- Precision sweep: estimated cost at each 10x coarser precision ----
    report.precision_estimates.push_back({report.precision, report.budget.total_bpp, report.estimated_file_bytes});
    if (report.digit_planes.size() > 1) {
        // Coarse passes sum leaf selection costs (ids + params + estimate
        // already included), so only the param-count byte and nodata flag
        // remain per block.
        const double coarse_overhead_per_block = 9.0;
        for (std::size_t k = 1; k < report.digit_planes.size(); ++k) {
            int32_t divisor = 1;
            for (std::size_t d = 0; d < k; ++d) divisor *= 10;

            terrain::IntGrid coarse = grid;
            for (int32_t& v : coarse.data) v /= divisor; // truncation, approximates rounding

            coding::PipelineContext ctx2(report.precision * static_cast<double>(divisor),
                                         ctx.context_model, ctx.pipeline_type,
                                         ctx.disable_quadtree, ctx.num_threads);
            PassResult pr = run_selection_pass(coarse, ctx2);

            double total_bits = pr.leaf_cost_bits + pr.structure_bits
                              + static_cast<double>(pr.blocks) * coarse_overhead_per_block;
            double bpp = total_bits / static_cast<double>(report.sample_count);
            double bytes = total_bits / 8.0 + static_cast<double>(pr.blocks) * 36.0 + 256.0;
            report.precision_estimates.push_back({ctx2.precision, bpp, bytes});
        }
    }

    return report;
}

} // namespace xtm::analyzer
