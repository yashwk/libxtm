#pragma once
#include "Predictor.hpp"

namespace xtm::predictor {

class LeftPredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Left"; }
};

class AbovePredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Above"; }
};

class AveragePredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Average"; }
};

class GradientPredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Gradient"; }
};

class JpegLsPredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "JPEG-LS"; }
};

class PlanePredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Plane"; }
};

class GapPredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "GAP (CALIC)"; }
};

class AdaptiveGradientPredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Adaptive Gradient"; }
};

class LeastSquaresPredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Least Squares"; }
};

class SecondOrderPredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Second Order"; }
};

class LocalSlopePredictor : public Predictor {
public:
    PredictionResult encode(const partition::BlockView& block) const override;
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override;
    const char* name() const override { return "Local Slope"; }
};

} // namespace xtm::predictor
