#include <gtest/gtest.h>
#include "xtm/predictor/Predictors.hpp"
#include <random>
#include <stdexcept>

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
    
    std::vector<int32_t> residuals;
    std::vector<int32_t> parameters;
    pred.encode(block, residuals, parameters);
    ASSERT_EQ(residuals.size(), original.data.size());
    
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
    
    pred.decode(residuals, parameters, mblock);
    
    for (size_t i = 0; i < original.data.size(); ++i) {
        ASSERT_EQ(original.data[i], decoded_grid.data[i]) << "Mismatch at index " << i << " for predictor " << pred.name();
    }
}

TEST(PredictorTest, LeftPredictorRoundTrip) {
    predictor::LeftPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 10, 1);
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

TEST(PredictorTest, PolynomialPredictorRoundTrip) {
    predictor::PolynomialPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}

TEST(PredictorTest, GapPredictorRoundTrip) {
    predictor::GapPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}


TEST(PredictorTest, LeastSquaresPredictorRoundTrip) {
    predictor::LeastSquaresPredictor p;
    test_roundtrip(p, 64, 64);
    test_roundtrip(p, 15, 20);
}



TEST(PredictorTest, PolynomialPredictorRejectsMissingParameters) {
    predictor::PolynomialPredictor p;
    terrain::IntGrid grid;
    grid.width = 16;
    grid.height = 16;
    grid.data.resize(16 * 16, 0);

    partition::MutableBlockView mblock;
    mblock.grid = &grid;
    mblock.x_offset = 0;
    mblock.y_offset = 0;
    mblock.width = 16;
    mblock.height = 16;

    std::vector<int32_t> residuals(16 * 16, 0);
    std::vector<int32_t> parameters;
    EXPECT_THROW(p.decode(residuals, parameters, mblock), std::runtime_error);
}

TEST(PredictorTest, LeastSquaresPredictorRejectsMissingParameters) {
    predictor::LeastSquaresPredictor p;
    terrain::IntGrid grid;
    grid.width = 16;
    grid.height = 16;
    grid.data.resize(16 * 16, 0);

    partition::MutableBlockView mblock;
    mblock.grid = &grid;
    mblock.x_offset = 0;
    mblock.y_offset = 0;
    mblock.width = 16;
    mblock.height = 16;

    std::vector<int32_t> residuals(16 * 16, 0);
    std::vector<int32_t> parameters;
    EXPECT_THROW(p.decode(residuals, parameters, mblock), std::runtime_error);
}

TEST(PredictorTest, PolynomialPredictorFitsCubicSurface) {
    predictor::PolynomialPredictor p;
    terrain::IntGrid grid;
    grid.width = 64;
    grid.height = 64;
    grid.data.resize(64 * 64, 0);

    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            double u = (x - 31.5) / 31.5;
            double v = (y - 31.5) / 31.5;
            double z = 5000.0 * (u*u*u - 3*u*v*v) + 2000.0 * (v*v*v - 3*v*u*u) + 1000.0 * u * v;
            grid.data[y * 64 + x] = static_cast<int32_t>(z);
        }
    }

    partition::MutableBlockView mblock;
    mblock.grid = &grid;
    mblock.x_offset = 0;
    mblock.y_offset = 0;
    mblock.width = 64;
    mblock.height = 64;

    partition::BlockView block;
    block.grid = mblock.grid;
    block.x_offset = mblock.x_offset;
    block.y_offset = mblock.y_offset;
    block.width = mblock.width;
    block.height = mblock.height;
    
    std::vector<int32_t> residuals;
    std::vector<int32_t> parameters;
    p.encode(block, residuals, parameters);

    EXPECT_EQ(parameters.size(), 11);

    int32_t max_res = 0;
    for (int32_t r : residuals) {
        max_res = std::max(max_res, std::abs(r));
    }
    EXPECT_LT(max_res, 100);

    terrain::IntGrid decoded_grid = grid;
    std::fill(decoded_grid.data.begin(), decoded_grid.data.end(), 0);
    partition::MutableBlockView dec_block;
    dec_block.grid = &decoded_grid;
    dec_block.x_offset = 0;
    dec_block.y_offset = 0;
    dec_block.width = 64;
    dec_block.height = 64;
    
    p.decode(residuals, parameters, dec_block);
    for (size_t i = 0; i < grid.data.size(); ++i) {
        EXPECT_EQ(decoded_grid.data[i], grid.data[i]);
    }
}
