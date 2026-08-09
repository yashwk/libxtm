#pragma once
#include "Predictor.hpp"
#include <memory>

namespace xtm::predictor {

class SplitPrecisionWrapper : public Predictor {
public:
    // precision_multiplier is e.g. 100 for scale 0.01
    // meter_predictor is the predictor chosen for the meter residuals.
    // precision_predictor is the predictor chosen for the precision residuals.
    SplitPrecisionWrapper(int32_t precision_multiplier, const Predictor* meter_predictor, const Predictor* precision_predictor);

    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Split Precision Wrapper"; }
    PredictorId id() const override { return meter_predictor_->id(); } // Identity matches the meter predictor

    // Helpers to access underlying models for stats/metadata
    const Predictor* meter_predictor() const { return meter_predictor_; }
    const Predictor* precision_predictor() const { return precision_predictor_; }

private:
    int32_t precision_multiplier_;
    const Predictor* meter_predictor_;
    const Predictor* precision_predictor_;
};

} // namespace xtm::predictor
