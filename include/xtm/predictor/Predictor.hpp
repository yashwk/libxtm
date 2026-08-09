#pragma once
#include "xtm/terrain/Quantization.hpp"
#include <vector>
#include <cstdint>

#include "xtm/partition/Block.hpp"

namespace xtm::predictor {

enum class PredictorId : uint8_t {
    Gradient,
    Left,
    JpegLs,
    Polynomial,
    Gap,
    LeastSquares,
};

struct PredictionResult {
    std::vector<int32_t> residuals;
    std::vector<int32_t> parameters;
};

class Predictor {
public:
    virtual ~Predictor() = default;

    virtual void encode(const partition::BlockView& block, PredictionResult& out_result) const = 0;
    virtual void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const = 0;
    
    virtual const char* name() const = 0;
    virtual PredictorId id() const = 0;
};

#define DECLARE_PREDICTOR(className, stringName, enumId) \
class className : public Predictor { \
public: \
    void encode(const partition::BlockView& block, PredictionResult& result) const override; \
    void decode(const PredictionResult& encoded, partition::MutableBlockView& block) const override; \
    const char* name() const override { return stringName; } \
    PredictorId id() const override { return PredictorId::enumId; } \
};

DECLARE_PREDICTOR(LeftPredictor, "Left", Left)
DECLARE_PREDICTOR(GradientPredictor, "Gradient", Gradient)
DECLARE_PREDICTOR(JpegLsPredictor, "JPEG-LS", JpegLs)
DECLARE_PREDICTOR(PolynomialPredictor, "Polynomial", Polynomial)
DECLARE_PREDICTOR(GapPredictor, "GAP (CALIC)", Gap)
DECLARE_PREDICTOR(LeastSquaresPredictor, "Least Squares", LeastSquares)

#undef DECLARE_PREDICTOR

struct PredictorBank {
    LeftPredictor left;
    GradientPredictor gradient;
    JpegLsPredictor jpegls;
    PolynomialPredictor polynomial;
    GapPredictor gap;
    LeastSquaresPredictor least_squares;

    std::vector<const Predictor*> ordered() const {
        return { &gradient, &left, &jpegls, &polynomial, &gap, &least_squares };
    }

    const Predictor* by_id(PredictorId id) const {
        for (const Predictor* p : ordered()) {
            if (p->id() == id) return p;
        }
        return nullptr;
    }
};

} // namespace xtm::predictor
