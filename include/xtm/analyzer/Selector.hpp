#pragma once
#include "xtm/predictor/Predictor.hpp"
#include <vector>

namespace xtm::analyzer {

struct SelectionResult {
    const predictor::Predictor* best_predictor;
    predictor::PredictionResult best_encoded;
    double total_bits;
    double bits_per_sample;
    bool use_wavelet = false;
    uint32_t wavelet_levels = 0;
};

enum class PipelineOrder {
    PredictorWavelet,
    WaveletPredictor
};

class PredictorSelector {
public:
    PredictorSelector(const std::vector<const predictor::Predictor*>& predictors, double early_exit_threshold = 10.0, PipelineOrder pipeline_order = PipelineOrder::PredictorWavelet);
    
    SelectionResult select(const partition::BlockView& block) const;

private:
    std::vector<const predictor::Predictor*> predictors_;
    double early_exit_threshold_;
    PipelineOrder pipeline_order_;
};

} // namespace xtm::analyzer
