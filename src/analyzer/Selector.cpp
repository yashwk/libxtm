#include "xtm/analyzer/Selector.hpp"
#include "xtm/analyzer/Statistics.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/ContextModeler.hpp"

#include "xtm/coding/ZigZag.hpp"
#include <limits>
#include <algorithm>
#include <cmath>

namespace xtm::analyzer {

double estimate_shannon_bits(const std::vector<int32_t>& residuals,
                             const coding::PipelineContext* pctx,
                             uint32_t width, uint32_t height,
                             double* magnitude_entropy,
                             double* run_entropy,
                             double* remainder_bits) {
    uint32_t mag_counts[8][33] = {{0}};
    uint64_t mag_totals[8] = {0};
    uint32_t run_counts[256] = {0};
    uint64_t run_total = 0;
    uint64_t rem_bits = 0;

    const auto accumulate = [&](uint8_t ctx, uint8_t mag, uint32_t run, uint32_t /*remainder*/) {
        if (run > 0) {
            mag_counts[ctx][0]++;
            mag_totals[ctx]++;
            run_counts[run - 1]++;
            run_total++;
        } else {
            mag_counts[ctx][mag]++;
            mag_totals[ctx]++;
            if (mag > 1) rem_bits += (mag - 1);
        }
    };

    if (pctx) {
        xtm::coding::walk_symbols(residuals, width, height, *pctx, accumulate);
    } else {
        // Fallback: single stream, Simple contexts.
        xtm::coding::PipelineContext simple_ctx(1.0, xtm::coding::ContextModel::Simple);
        xtm::coding::walk_symbols(residuals, width, height, simple_ctx, accumulate);
    }

    double mag_entropy = 0.0;
    for (uint32_t c = 0; c < 8; ++c) {
        if (mag_totals[c] == 0) continue;
        double inv_mags = 1.0 / static_cast<double>(mag_totals[c]);
        for (int i = 0; i <= 32; ++i) {
            if (mag_counts[c][i] > 0) {
                double p = mag_counts[c][i] * inv_mags;
                mag_entropy -= mag_counts[c][i] * std::log2(p);
            }
        }
    }

    double run_ent = 0.0;
    if (run_total > 0) {
        double inv_runs = 1.0 / static_cast<double>(run_total);
        for (int i = 0; i <= 255; ++i) {
            if (run_counts[i] > 0) {
                double p = run_counts[i] * inv_runs;
                run_ent -= run_counts[i] * std::log2(p);
            }
        }
    }

    if (magnitude_entropy) *magnitude_entropy = mag_entropy;
    if (run_entropy) *run_entropy = run_ent;
    if (remainder_bits) *remainder_bits = static_cast<double>(rem_bits);
    return static_cast<double>(rem_bits) + mag_entropy + run_ent;
}

PredictorSelector::PredictorSelector(const std::vector<const predictor::Predictor*>& predictors, const coding::PipelineContext& ctx)
    : predictors_(predictors), ctx_(ctx) {}

SelectionResult PredictorSelector::select(const partition::BlockView& block) const {
    SelectionResult best_result;
    best_result.best_predictor = nullptr;
    best_result.total_bits = std::numeric_limits<double>::infinity();
    
    int num_samples = block.width * block.height;

    // Per-plane estimate context: the coder codes each plane as its own
    // stream, so only the concatenated [meter; precision] winner residuals
    // are scored with the split-precision flag set.
    coding::PipelineContext plane_ctx = ctx_;
    plane_ctx.has_precision = false;

    if (ctx_.pipeline_type == PipelineType::Wavelet) {
        uint32_t max_levels = coding::max_wavelet_levels(block.width, block.height);
        
        best_result.best_residuals.resize(num_samples);
        for (uint32_t y = 0; y < block.height; ++y) {
            for (uint32_t x = 0; x < block.width; ++x) {
                best_result.best_residuals[y * block.width + x] = block.get(x, y);
            }
        }
        
        if (max_levels > 0) {
            transform::CDF53Transform::forward_2d(best_result.best_residuals, block.width, block.height, max_levels);
        }
        
        double c_residual = estimate_shannon_bits(best_result.best_residuals, &plane_ctx, block.width, block.height);
        
        best_result.total_bits = c_residual;
        best_result.wavelet_levels = max_levels;
        best_result.bits_per_sample = best_result.total_bits / num_samples;
        return best_result;
    }

    // Quick Terrain Classification (V12.1)
    int32_t min_val = block.row_data(0)[0];
    int32_t max_val = min_val;
    for (uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t val = row[x];
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
        }
    }
    int32_t delta = max_val - min_val;
    
    std::vector<const predictor::Predictor*> active_predictors;
    if (delta == 0) {
        for (auto* p : predictors_) {
            if (p->id() == predictor::PredictorId::Left) {
                active_predictors.push_back(p);
            }
        }
    } else if (delta > 200) {
        for (auto* p : predictors_) {
            if (p->id() != predictor::PredictorId::Polynomial &&
                p->id() != predictor::PredictorId::JpegLs) {
                active_predictors.push_back(p);
            }
        }
    } else {
        active_predictors = predictors_;
    }
    
    if (active_predictors.empty()) active_predictors = predictors_;
    
    bool has_precision = ctx_.has_precision;
    
    terrain::IntGrid temp_meter;
    terrain::IntGrid temp_prec;
    partition::BlockView m_view = block;
    partition::BlockView p_view = block;
    
    if (has_precision) {
        temp_meter.width = block.grid->width;
        temp_meter.height = block.grid->height;
        temp_meter.data.resize(temp_meter.width * temp_meter.height, 0);
        
        temp_prec.width = block.grid->width;
        temp_prec.height = block.grid->height;
        temp_prec.data.resize(temp_prec.width * temp_prec.height, 0);
        
        uint32_t start_x = (block.x_offset > 0) ? block.x_offset - 1 : 0;
        uint32_t start_y = (block.y_offset > 0) ? block.y_offset - 1 : 0;
        uint32_t end_x = block.x_offset + block.width;
        uint32_t end_y = block.y_offset + block.height;
        
        for (uint32_t y = start_y; y < end_y; ++y) {
            for (uint32_t x = start_x; x < end_x; ++x) {
                int32_t z = block.grid->get(x, y);
                uint32_t idx = y * temp_meter.width + x;
                temp_meter.data[idx] = z / ctx_.precision_multiplier;
                temp_prec.data[idx] = z % ctx_.precision_multiplier;
            }
        }
        
        m_view.grid = &temp_meter;
        p_view.grid = &temp_prec;
    }

    const predictor::Predictor* best_m_pred = nullptr;
    double best_m_bits = std::numeric_limits<double>::infinity();
    std::vector<int32_t> best_m_res_residuals;
    std::vector<int32_t> best_m_res_parameters;
    bool best_m_sec = false;

    // Per-candidate second-order RDO. For every primary candidate the
    // residual pool is evaluated on that candidate's residuals: the choice
    // of primary predictor and residual predictor is coupled (a residual
    // stage can make a different primary predictor win), so the cheapest
    // (primary, residual) pair wins per block.
    //
    // Pool: None (baseline), Average (W/2+N/2), Median (W,N,NW), Left,
    // Gradient, Gap, LeastSquares. Left/Gradient/Gap/LS run the primary
    // predictor classes over a zero-bordered view of the residual plane;
    // Average/Median are inline kernels. Only the meter plane gets this
    // stage (the precision plane is predicted once, with simple predictors
    // only). The 3-bit id lives in the already-written predictor byte, so
    // the signal costs nothing; params are charged 32 bits. A pool member
    // must beat the plain residual estimate by more than 16 bits to be
    // accepted: the Shannon estimate cannot see the adaptive run-table
    // overhead, so marginal wins would lose in the real coder.
    predictor::PredictorBank bank;
    for (const auto* pred : active_predictors) {
            scratch_residuals_.clear();
            scratch_parameters_.clear();
            pred->encode(m_view, scratch_residuals_, scratch_parameters_);

            const size_t n_samples = static_cast<size_t>(num_samples);
            size_t zeros = 0;
            for (int32_t v : scratch_residuals_) {
                if (v == 0) zeros++;
            }

            double base_resid_bits = estimate_shannon_bits(scratch_residuals_, &plane_ctx, block.width, block.height);
            double best_resid_bits = base_resid_bits; // None baseline
            ResidualPredictorId resid_pid = ResidualPredictorId::None;
            const predictor::Predictor* best_resid_pred = nullptr;
            std::vector<int32_t> best_resid_params;
            scratch_winner_res_.clear();

            const auto consider = [&](ResidualPredictorId pid, const predictor::Predictor* pred2,
                                      const std::vector<int32_t>& r2, const std::vector<int32_t>& rparams) {
                double c = rparams.size() * 32.0
                         + estimate_shannon_bits(r2, &plane_ctx, block.width, block.height);
                double thresh = (resid_pid == ResidualPredictorId::None) ? best_resid_bits - 16.0 : best_resid_bits;
                if (c < thresh) {
                    best_resid_bits = c;
                    resid_pid = pid;
                    best_resid_pred = pred2;
                    best_resid_params = rparams;
                    if (pid != ResidualPredictorId::None) scratch_winner_res_ = r2;
                }
            };

            if (zeros * 100 <= n_samples * 95) {
                // Average: p = W/2 + N/2. First row/column peeled so the
                // interior loop has no boundary branches (vectorizable).
                scratch_sec_res_.resize(n_samples);
                scratch_sec_res_[0] = scratch_residuals_[0];
                for (uint32_t x = 1; x < block.width; ++x) {
                    scratch_sec_res_[x] = scratch_residuals_[x] - scratch_residuals_[x - 1];
                }
                for (uint32_t y = 1; y < block.height; ++y) {
                    scratch_sec_res_[y * block.width] =
                        scratch_residuals_[y * block.width] - scratch_residuals_[(y - 1) * block.width];
                }
                {
                    const uint32_t w = block.width;
                    const int32_t* src = scratch_residuals_.data();
                    int32_t* dst = scratch_sec_res_.data();
                    for (uint32_t y = 1; y < block.height; ++y) {
                        const int32_t* row = src + y * w;
                        const int32_t* above = row - w;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
                        for (uint32_t x = 1; x < w; ++x) {
                            dst[y * w + x] = row[x] - row[x - 1] / 2 - above[x] / 2;
                        }
                    }
                }
                consider(ResidualPredictorId::Average, nullptr, scratch_sec_res_, std::vector<int32_t>{});

                // Median of (W, N, NW)
                scratch_resid_res_.resize(n_samples);
                scratch_resid_res_[0] = scratch_residuals_[0];
                for (uint32_t x = 1; x < block.width; ++x) {
                    scratch_resid_res_[x] = scratch_residuals_[x] - scratch_residuals_[x - 1];
                }
                for (uint32_t y = 1; y < block.height; ++y) {
                    scratch_resid_res_[y * block.width] =
                        scratch_residuals_[y * block.width] - scratch_residuals_[(y - 1) * block.width];
                }
                {
                    const uint32_t w = block.width;
                    const int32_t* src = scratch_residuals_.data();
                    int32_t* dst = scratch_resid_res_.data();
                    for (uint32_t y = 1; y < block.height; ++y) {
                        const int32_t* row = src + y * w;
                        const int32_t* above = row - w;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
                        for (uint32_t x = 1; x < w; ++x) {
                            int32_t wv = row[x - 1];
                            int32_t nv = above[x];
                            int32_t nwv = above[x - 1];
                            int32_t p = std::max(std::min(wv, nv), std::min(std::max(wv, nv), nwv));
                            dst[y * w + x] = row[x] - p;
                        }
                    }
                }
                consider(ResidualPredictorId::Median, nullptr, scratch_resid_res_, std::vector<int32_t>{});

                // Left / Gradient / Gap / Least Squares over a zero-bordered
                // view of the residual plane. Cells outside the block read as
                // 0 on both sides (encode builds the same plane the decoder
                // reconstructs).
                terrain::IntGrid rgrid;
                rgrid.width = block.width + 2;
                rgrid.height = block.height + 2;
                rgrid.data.assign(rgrid.width * rgrid.height, 0);
                for (uint32_t y = 0; y < block.height; ++y) {
                    for (uint32_t x = 0; x < block.width; ++x) {
                        rgrid.data[(y + 1) * rgrid.width + (x + 1)] = scratch_residuals_[y * block.width + x];
                    }
                }
                partition::BlockView rview;
                rview.grid = &rgrid;
                rview.x_offset = 1;
                rview.y_offset = 1;
                rview.width = block.width;
                rview.height = block.height;

                struct PoolEntry {
                    ResidualPredictorId pid;
                    const predictor::Predictor* pred;
                };
                const PoolEntry pool[] = {
                    {ResidualPredictorId::Left, &bank.left},
                    {ResidualPredictorId::Gradient, &bank.gradient},
                    {ResidualPredictorId::Gap, &bank.gap},
                    {ResidualPredictorId::LeastSquares, &bank.least_squares},
                };
                for (const auto& e : pool) {
                    scratch_resid_res_.clear();
                    scratch_resid_params_.clear();
                    e.pred->encode(rview, scratch_resid_res_, scratch_resid_params_);
                    consider(e.pid, e.pred, scratch_resid_res_, scratch_resid_params_);
                }
            }

            double score = 8.0 + scratch_parameters_.size() * 32.0 + best_resid_bits;
            if (score < best_m_bits) {
                best_m_bits = score;
                best_m_pred = pred;
                best_m_res_residuals = (resid_pid != ResidualPredictorId::None)
                    ? scratch_winner_res_ : scratch_residuals_;
                best_m_res_parameters = scratch_parameters_;
                best_m_sec = (resid_pid != ResidualPredictorId::None);
                double resid_params_bits = best_resid_params.size() * 32.0;
                best_result.residual_predictor_id = resid_pid;
                best_result.best_residual_predictor = best_resid_pred;
                best_result.best_residual_parameters = std::move(best_resid_params);
                best_result.second_order_bits_savings = best_m_sec
                    ? (base_resid_bits - (best_resid_bits - resid_params_bits))
                    : 0.0;
                best_result.base_bits = base_resid_bits;
            }
        }
    
    const predictor::Predictor* best_p_pred = nullptr;
    bool best_p_raw = false;
    double best_p_bits = 0;
    std::vector<int32_t> best_p_res_residuals;
    std::vector<int32_t> best_p_res_parameters;
    
    if (has_precision) {
        best_p_bits = std::numeric_limits<double>::infinity();
        std::vector<const predictor::Predictor*> prec_candidates;
        for (auto* p : predictors_) {
            if (p->id() == predictor::PredictorId::Left || p->id() == predictor::PredictorId::Gradient || p->id() == predictor::PredictorId::Gap) {
                prec_candidates.push_back(p);
            }
        }
        
        for (const auto* pred : prec_candidates) {
            scratch_residuals_.clear();
            scratch_parameters_.clear();
            pred->encode(p_view, scratch_residuals_, scratch_parameters_);
            double actual_bits = estimate_shannon_bits(scratch_residuals_, &plane_ctx, block.width, block.height);
            
            double c_id = 8.0; 
            double total_bits = c_id + actual_bits; 
            
            if (total_bits < best_p_bits) {
                best_p_bits = total_bits;
                best_p_pred = pred;
                best_p_raw = false;
                best_p_res_residuals = scratch_residuals_;
                best_p_res_parameters = scratch_parameters_;
            }
        }
        
        // Identity (raw passthrough): no prediction. Wins when the precision
        // digits are already incompressible (e.g. uniform noise), where any
        // predictor only widens the residual support and the zero-run coding
        // costs more than the runs it creates. Signaled as 0xFE; the decoder
        // then treats the coded precision stream as the digit plane itself.
        scratch_resid_res_.resize(static_cast<size_t>(num_samples));
        for (uint32_t y = 0; y < block.height; ++y) {
            for (uint32_t x = 0; x < block.width; ++x) {
                scratch_resid_res_[y * block.width + x] = p_view.get(x, y);
            }
        }
        double raw_bits = estimate_shannon_bits(scratch_resid_res_, &plane_ctx, block.width, block.height);
        if (raw_bits + 8.0 < best_p_bits) {
            best_p_bits = raw_bits + 8.0;
            best_p_pred = nullptr;
            best_p_raw = true;
            best_p_res_residuals = scratch_resid_res_;
            best_p_res_parameters.clear();
        }
    }
    
    best_result.total_bits = best_m_bits + (has_precision ? best_p_bits : 0);
    best_result.bits_per_sample = best_result.total_bits / num_samples;
    best_result.best_predictor = best_m_pred;
    best_result.best_prec_predictor = best_p_pred;
    best_result.best_prec_raw = best_p_raw;
    best_result.use_second_order = best_m_sec;
    
    if (has_precision) {
        best_result.best_residuals = std::move(best_m_res_residuals);
        best_result.best_residuals.insert(best_result.best_residuals.end(), best_p_res_residuals.begin(), best_p_res_residuals.end());
        best_result.best_parameters = std::move(best_m_res_parameters);
        best_result.best_parameters.insert(best_result.best_parameters.end(), best_p_res_parameters.begin(), best_p_res_parameters.end());
    } else {
        best_result.best_residuals = std::move(best_m_res_residuals);
        best_result.best_parameters = std::move(best_m_res_parameters);
    }

    return best_result;
}

} // namespace xtm::analyzer
