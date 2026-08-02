#include <gtest/gtest.h>
#include "xtm/predictor/Predictors.hpp"
#include <random>

using namespace xtm;

terrain::IntGrid create_random_grid(uint32_t width, uint32_t height, int seed = 42) {
    terrain::IntGrid grid;
    grid.width = width;
    grid.height = height;
    grid.data.resize(width * height);
    
    std::mt19937 gen(seed);
    std::uniform_int_distribution<int32_t> dist(-1000, 5000);
    
    for (auto& val : grid.data) {
        val = dist(gen);
    }
    return grid;
}

void test_roundtrip(const predictor::Predictor& pred, uint32_t width, uint32_t height) {
    terrain::IntGrid original = create_random_grid(width, height);
    
    partition::BlockView block;
    block.grid = &original;
    block.x_offset = 0;
    block.y_offset = 0;
    block.width = width;
    block.height = height;
    
    predictor::PredictionResult encoded;
    pred.encode(block, encoded);
    ASSERT_EQ(encoded.residuals.size(), original.data.size());
    
    terrain::IntGrid decoded_grid;
    decoded_grid.width = width;
    decoded_grid.height = height;
    decoded_grid.data.resize(width * height);
    
    partition::MutableBlockView mblock;
    mblock.grid = &decoded_grid;
    mblock.x_offset = 0;
    mblock.y_offset = 0;
    mblock.width = width;
    mblock.height = height;
    
    pred.decode(encoded, mblock);
    
    for (size_t i = 0; i < original.data.size(); ++i) {
        ASSERT_EQ(original.data[i], decoded_grid.data[i]) << "Mismatch at index " << i << " for predictor " << pred.name();
    }
}

TEST(PredictorTest, LeftPredictorRoundTrip) {
    predictor::LeftPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 10, 1);
}

TEST(PredictorTest, AbovePredictorRoundTrip) {
    predictor::AbovePredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 1, 10);
}

TEST(PredictorTest, AveragePredictorRoundTrip) {
    predictor::AveragePredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 10, 10);
}

TEST(PredictorTest, GradientPredictorRoundTrip) {
    predictor::GradientPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, JpegLsPredictorRoundTrip) {
    predictor::JpegLsPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, PlanePredictorRoundTrip) {
    predictor::PlanePredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, GapPredictorRoundTrip) {
    predictor::GapPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, AdaptiveGradientPredictorRoundTrip) {
    predictor::AdaptiveGradientPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, LeastSquaresPredictorRoundTrip) {
    predictor::LeastSquaresPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, SecondOrderPredictorRoundTrip) {
    predictor::SecondOrderPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, LocalSlopePredictorRoundTrip) {
    predictor::LocalSlopePredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}
