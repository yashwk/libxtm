#pragma once
#include "Predictor.hpp"
#include <memory>
#include <vector>

namespace xtm::predictor {

class LeftPredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Left"; }
};

class AbovePredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Above"; }
};

class AveragePredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Average"; }
};

class GradientPredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Gradient"; }
};

class JpegLsPredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "JPEG-LS"; }
};

class PlanePredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Plane"; }
};

class GapPredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "GAP (CALIC)"; }
};

class AdaptiveGradientPredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Adaptive Gradient"; }
};

class LeastSquaresPredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Least Squares"; }
};

class SecondOrderPredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Second Order"; }
};

class LocalSlopePredictor : public Predictor {
public:
    void encode(const partition::BlockView& block, PredictionResult& result) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Local Slope"; }
};

// Central construction point for the predictor bank, so that the CLI encoder,
// CLI decoder, and the analyzer cannot drift out of sync.
struct PredictorBank {
    std::unique_ptr<LeftPredictor> left = std::make_unique<LeftPredictor>();
    std::unique_ptr<AbovePredictor> above = std::make_unique<AbovePredictor>();
    std::unique_ptr<AveragePredictor> average = std::make_unique<AveragePredictor>();
    std::unique_ptr<GradientPredictor> gradient = std::make_unique<GradientPredictor>();
    std::unique_ptr<JpegLsPredictor> jpegls = std::make_unique<JpegLsPredictor>();
    std::unique_ptr<PlanePredictor> plane = std::make_unique<PlanePredictor>();
    std::unique_ptr<GapPredictor> gap = std::make_unique<GapPredictor>();
    std::unique_ptr<AdaptiveGradientPredictor> adaptive_gradient = std::make_unique<AdaptiveGradientPredictor>();
    std::unique_ptr<LeastSquaresPredictor> least_squares = std::make_unique<LeastSquaresPredictor>();
    std::unique_ptr<SecondOrderPredictor> second_order = std::make_unique<SecondOrderPredictor>();
    std::unique_ptr<LocalSlopePredictor> local_slope = std::make_unique<LocalSlopePredictor>();

    // Predictor order serialized into the .xtm stream (predictor ID). Do not reorder.
    std::vector<const Predictor*> ordered() const {
        return { gradient.get(), left.get(), above.get(), average.get(), jpegls.get(), plane.get(),
                 gap.get(), adaptive_gradient.get(), least_squares.get(), second_order.get(), local_slope.get() };
    }
};

} // namespace xtm::predictor
