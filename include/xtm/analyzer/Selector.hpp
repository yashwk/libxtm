#pragma once
#include "xtm/predictor/Predictor.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include "xtm/analyzer/PipelineType.hpp"
#include <vector>

namespace xtm::analyzer {

// Shannon-based estimate of the coded bit cost of a residual stream, mirroring
// the encoder's symbol model (see walk_symbols in ContextModeler.hpp):
//   - per-context magnitude-class histograms (stream x neighbour-activity,
//     exactly the FrequencyTables the coder maintains), zero runs counted as
//     class-0 symbols in the run's start context;
//   - one shared zero-run-length histogram (the coder's shared run table);
//   - fixed remainder bits for magnitudes > 1.
// When pctx->has_precision is set, `residuals` is the concatenation
// [meter (N), precision (N)] and the streams are scored separately, exactly
// like encode_stream. `width`/`height` are the block dimensions (per plane).
// The optional out-params expose the per-component split of the total:
//   magnitude_entropy - total per-context entropy of the magnitude classes
//   run_entropy       - entropy of the zero-run lengths
//   remainder_bits    - fixed remainder bits for magnitudes > 1
double estimate_shannon_bits(const std::vector<int32_t>& residuals,
                             const coding::PipelineContext* pctx,
                             uint32_t width, uint32_t height,
                             double* magnitude_entropy = nullptr,
                             double* run_entropy = nullptr,
                             double* remainder_bits = nullptr);

// Second-order (residual-plane) predictor ids, serialized in the 3 high bits
// of the predictor byte. 1-2 are implemented inline (they are simple kernels);
// 3-6 reuse the primary predictor classes on a residual view.
enum class ResidualPredictorId : uint8_t {
    None = 0,
    Average = 1,   // p = W/2 + N/2
    Median = 2,    // p = median(W, N, NW)
    Left = 3,
    Gradient = 4,
    Gap = 5,
    LeastSquares = 6,
};

struct SelectionResult {
    const predictor::Predictor* best_predictor;
    const predictor::Predictor* best_prec_predictor = nullptr;
    // true when the precision plane is coded raw (no prediction, 0xFE marker):
    // chosen when the precision digits are incompressible.
    bool best_prec_raw = false;
    std::vector<int32_t> best_residuals;
    std::vector<int32_t> best_parameters;
    double total_bits;
    double bits_per_sample = 0;
    uint32_t wavelet_levels = 0;
    bool use_second_order = false;

    // Second-order residual re-prediction on the meter residuals.
    // best_residuals holds the coded stream (residuals-of-residuals when
    // residual_predictor_id != None); the decoder reverses this stage before
    // the primary predictor.
    ResidualPredictorId residual_predictor_id = ResidualPredictorId::None;
    const predictor::Predictor* best_residual_predictor = nullptr;
    std::vector<int32_t> best_residual_parameters;

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
    mutable std::vector<int32_t> scratch_resid_res_;
    mutable std::vector<int32_t> scratch_resid_params_;
    mutable std::vector<int32_t> scratch_winner_res_;
};

} // namespace xtm::analyzer
