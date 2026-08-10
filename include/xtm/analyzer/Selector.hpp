#pragma once
#include "xtm/predictor/Predictor.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include "xtm/analyzer/PipelineType.hpp"
#include <vector>

namespace xtm::analyzer {

// Shannon-based estimate of the coded bit cost of a residual stream, mirroring
// the encoder's zigzag cost model (magnitude classes + zero runs + remainder
// bits). The optional out-params expose the per-component split of the total:
//   magnitude_entropy - entropy of the magnitude classes (zero runs counted as
//                       class-0 symbols)
//   run_entropy       - entropy of the zero-run lengths
//   remainder_bits    - fixed remainder bits for magnitudes > 1
double estimate_shannon_bits(const std::vector<int32_t>& residuals,
                             double* magnitude_entropy = nullptr,
                             double* run_entropy = nullptr,
                             double* remainder_bits = nullptr);

struct SelectionResult {
    const predictor::Predictor* best_predictor;
    const predictor::Predictor* best_prec_predictor = nullptr;
    std::vector<int32_t> best_residuals;
    std::vector<int32_t> best_parameters;
    double total_bits;
    double bits_per_sample = 0;
    uint32_t wavelet_levels = 0;
    bool use_second_order = false;
    
    double base_bits = 0.0;
    double second_order_bits_savings = 0.0;
};

class PredictorSelector {
public:
    PredictorSelector(const std::vector<const predictor::Predictor*>& predictors, const coding::PipelineContext& ctx);
    
    SelectionResult select(const partition::BlockView& block) const;

private:
    std::vector<const predictor::Predictor*> predictors_;
    coding::PipelineContext ctx_;
    mutable std::vector<int32_t> scratch_residuals_;
    mutable std::vector<int32_t> scratch_parameters_;
    mutable std::vector<int32_t> scratch_wv_data_;
    mutable std::vector<int32_t> scratch_sec_res_;
};

} // namespace xtm::analyzer
