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
                             double* magnitude_entropy,
                             double* run_entropy,
                             double* remainder_bits) {
    uint32_t counts[33] = {0};
    uint32_t run_counts[256] = {0};
    uint32_t total_mags = 0;
    uint32_t total_runs = 0;
    uint32_t rem_bits = 0;

    uint32_t zero_run = 0;
    for (int32_t val : residuals) {
        if (val == 0) {
            zero_run++;
            if (zero_run == 255) {
                counts[0]++; total_mags++;
                run_counts[255]++; total_runs++;
                zero_run = 0;
            }
        } else {
            if (zero_run > 0) {
                counts[0]++; total_mags++;
                run_counts[zero_run]++; total_runs++;
                zero_run = 0;
            }
            uint32_t zz = xtm::coding::zigzag_encode(val);
            uint32_t mag = xtm::coding::get_magnitude_class(zz);
            counts[mag]++; total_mags++;
            if (mag > 1) rem_bits += (mag - 1);
        }
    }
    if (zero_run > 0) {
        counts[0]++; total_mags++;
        run_counts[zero_run]++; total_runs++;
    }

    double mag_entropy = 0.0;
    if (total_mags > 0) {
        double inv_mags = 1.0 / total_mags;
        for (int i = 0; i <= 32; ++i) {
            if (counts[i] > 0) {
                double p = counts[i] * inv_mags;
                mag_entropy -= counts[i] * std::log2(p);
            }
        }
    }

    double run_ent = 0.0;
    if (total_runs > 0) {
        double inv_runs = 1.0 / total_runs;
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
    return rem_bits + mag_entropy + run_ent;
}

PredictorSelector::PredictorSelector(const std::vector<const predictor::Predictor*>& predictors, const coding::PipelineContext& ctx)
    : predictors_(predictors), ctx_(ctx) {}

SelectionResult PredictorSelector::select(const partition::BlockView& block) const {
    SelectionResult best_result;
    best_result.best_predictor = nullptr;
    best_result.total_bits = std::numeric_limits<double>::infinity();
    
    int num_samples = block.width * block.height;
    
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
        
        double c_residual = estimate_shannon_bits(best_result.best_residuals);
        
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
    
    for (const auto* pred : active_predictors) {
        scratch_residuals_.clear();
        scratch_parameters_.clear();
        pred->encode(m_view, scratch_residuals_, scratch_parameters_);
        
        double c_id = 8.0; 
        double c_params = scratch_parameters_.size() * 32.0;
        
        double actual_bits = estimate_shannon_bits(scratch_residuals_);
        
        double score = c_id + c_params + actual_bits;
        
        scratch_sec_res_.resize(num_samples);
        for (uint32_t y = 0; y < block.height; ++y) {
            for (uint32_t x = 0; x < block.width; ++x) {
                int32_t r = scratch_residuals_[y * block.width + x];
                int32_t w_val = (x > 0) ? scratch_residuals_[y * block.width + x - 1] : 0;
                int32_t n_val = (y > 0) ? scratch_residuals_[(y - 1) * block.width + x] : 0;
                int32_t p = 0;
                if (x > 0 && y > 0) p = w_val / 2 + n_val / 2;
                else if (x > 0) p = w_val;
                else if (y > 0) p = n_val;
                int32_t sr = r - p;
                scratch_sec_res_[y * block.width + x] = sr;
            }
        }
        
        double sec_actual_bits = estimate_shannon_bits(scratch_sec_res_);
        
        double sec_score = c_id + c_params + sec_actual_bits + 16.0;
        
        bool use_sec = false;
        if (sec_score < score) {
            score = sec_score;
            scratch_residuals_ = scratch_sec_res_;
            use_sec = true;
        }
        
        if (score < best_m_bits) {
            best_m_bits = score;
            best_m_pred = pred;
            best_m_res_residuals = scratch_residuals_;
            best_m_res_parameters = scratch_parameters_;
            best_m_sec = use_sec;
            best_result.second_order_bits_savings = use_sec ? (actual_bits - sec_actual_bits) : 0.0;
            best_result.base_bits = actual_bits;
        }
    }
    
    const predictor::Predictor* best_p_pred = nullptr;
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
            double actual_bits = estimate_shannon_bits(scratch_residuals_);
            
            double c_id = 8.0; 
            double total_bits = c_id + actual_bits; 
            
            if (total_bits < best_p_bits) {
                best_p_bits = total_bits;
                best_p_pred = pred;
                best_p_res_residuals = scratch_residuals_;
                best_p_res_parameters = scratch_parameters_;
            }
        }
    }
    
    best_result.total_bits = best_m_bits + (has_precision ? best_p_bits : 0);
    best_result.bits_per_sample = best_result.total_bits / num_samples;
    best_result.best_predictor = best_m_pred;
    best_result.best_prec_predictor = best_p_pred;
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
