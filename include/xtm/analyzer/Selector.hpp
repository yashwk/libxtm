#pragma once
#include "xtm/predictor/Predictor.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include <vector>

namespace xtm::analyzer {

struct SelectionResult {
    const predictor::Predictor* best_predictor;
    const predictor::Predictor* best_prec_predictor = nullptr;
    predictor::PredictionResult best_encoded;
    double total_bits;
    double bits_per_sample = 0;
    uint32_t wavelet_levels = 0;
    bool use_second_order = false;
    
    double base_bits = 0.0;
    double second_order_bits_savings = 0.0;
};

enum class PipelineType {
    Predictor,
    Wavelet
};

class PredictorSelector {
public:
    PredictorSelector(const std::vector<const predictor::Predictor*>& predictors, PipelineType pipeline_type = PipelineType::Predictor, int32_t precision_multiplier = 1, coding::ContextModel context_model = coding::ContextModel::Extended);
    
    SelectionResult select(const partition::BlockView& block) const;

private:
    std::vector<const predictor::Predictor*> predictors_;
    PipelineType pipeline_type_;
    int32_t precision_multiplier_;
    coding::ContextModel context_model_;
    mutable predictor::PredictionResult scratch_encoded_;
    mutable std::vector<int32_t> scratch_wv_data_;
    mutable std::vector<int32_t> scratch_sec_res_;
};

} // namespace xtm::analyzer
