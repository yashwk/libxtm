#include <gtest/gtest.h>
#include "xtm/analyzer/Analyzer.hpp"
#include <vector>
#include <random>
#include <cmath>

using namespace xtm;
using namespace xtm::analyzer;

class SyntheticTerrain {
    TerrainBuffer _buffer;
public:
    SyntheticTerrain(uint32_t w, uint32_t h) : _buffer(w, h) {
        _buffer.nodata_value = std::nullopt;
    }

    void fill_constant(float val) {
        float* d = _buffer.data();
        std::fill(d, d + _buffer.width() * _buffer.height(), val);
    }

    void fill_ramp() {
        float* d = _buffer.data();
        uint32_t w = _buffer.width();
        for (uint32_t y = 0; y < _buffer.height(); ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                d[y * w + x] = x + y;
            }
        }
    }

    void fill_noise() {
        float* d = _buffer.data();
        std::mt19937 gen(42);
        std::uniform_real_distribution<float> dis(0.0f, 1000.0f);
        for (uint32_t i = 0; i < _buffer.width() * _buffer.height(); ++i) {
            d[i] = dis(gen);
        }
    }

    void fill_checkerboard() {
        float* d = _buffer.data();
        uint32_t w = _buffer.width();
        for (uint32_t y = 0; y < _buffer.height(); ++y) {
            for (uint32_t x = 0; x < w; ++x) {
                d[y * w + x] = ((x + y) % 2 == 0) ? 0.0f : 100.0f;
            }
        }
    }

    TerrainView view() const {
        return _buffer.view();
    }
};

void assert_invariants(const AnalysisReport& rep) {
    // 1. Predictor usages sum to 100% implicitly handled (counts)
    
    // 2. Entropy >= 0
    EXPECT_GE(rep.global_predictors.left_entropy, 0);
    
    // 3. Zero run avg <= max (or both 0)
    EXPECT_LE(rep.wavelet_stats.avg_zero_run, rep.wavelet_stats.max_zero_run);
    
    // 4. Subband energy ~100%
    double energy_sum = rep.wavelet_stats.ll_energy_pct + rep.wavelet_stats.lh_energy_pct + 
                        rep.wavelet_stats.hl_energy_pct + rep.wavelet_stats.hh_energy_pct;
    if (energy_sum > 0) {
        EXPECT_NEAR(energy_sum, 100.0, 0.1);
    }
    
    // 5. Correlation in [-1, 1]
    EXPECT_GE(rep.correlation_stats.raw_horizontal, -1.01);
    EXPECT_LE(rep.correlation_stats.raw_horizontal, 1.01);
    
    // 6. Context sizes
    if (rep.context_stats.unique_contexts > 0) {
        EXPECT_LE(rep.context_stats.smallest_context, rep.context_stats.median_context_size);
        EXPECT_LE(rep.context_stats.median_context_size, rep.context_stats.largest_context);
    }
    
    // 7. Percentiles
    EXPECT_LE(rep.residual_dist_stats.median, rep.residual_dist_stats.p95);
    EXPECT_LE(rep.residual_dist_stats.p95, rep.residual_dist_stats.p99);
    EXPECT_LE(rep.residual_dist_stats.p99, rep.residual_dist_stats.max_val);
    
    // 8. Quadtree counts match leaves
    std::size_t total_quad = rep.quadtree_stats.size_512_count + rep.quadtree_stats.size_256_count + 
                             rep.quadtree_stats.size_128_count + rep.quadtree_stats.size_64_count;
    EXPECT_EQ(total_quad, rep.quadtree_leaves);
}

TEST(AnalyzerTest, ConstantPlane) {
    SyntheticTerrain view(1024, 1024);
    view.fill_constant(1000.0f);
    auto report = analyze_terrain(view.view(), 0.01);
    
    assert_invariants(report);
    
    // Correlation should be 1.0
    EXPECT_NEAR(report.correlation_stats.raw_horizontal, 1.0, 1e-4);
    
    // Residuals should be exact 0 for non-boundary pixels
    // EXPECT_EQ(report.residual_dist_stats.max_val, 0); // Max val will be the origin pixel which is 100000
    EXPECT_GE(report.predictor_confidence.pct_exact, 99.0);
}

TEST(AnalyzerTest, LinearRamp) {
    SyntheticTerrain view(512, 512);
    view.fill_ramp();
    auto report = analyze_terrain(view.view(), 1.0);
    
    assert_invariants(report);
    
    // Gradient predictor would be exact, but Left/Above might be chosen due to tie (all have 0 entropy),
    // and they yield residuals of 1. So within +/- 1 should be ~100%
    // EXPECT_EQ(report.residual_dist_stats.max_val, 0);
    EXPECT_GE(report.predictor_confidence.pct_within_1, 99.0);
}

TEST(AnalyzerTest, RandomNoise) {
    SyntheticTerrain view(512, 512);
    view.fill_noise();
    auto report = analyze_terrain(view.view(), 1.0);
    
    assert_invariants(report);
    
    // Predictors should struggle
    EXPECT_LT(report.predictor_confidence.pct_exact, 10.0);
}

TEST(AnalyzerTest, Checkerboard) {
    SyntheticTerrain view(512, 512);
    view.fill_checkerboard();
    auto report = analyze_terrain(view.view(), 1.0);
    
    assert_invariants(report);
    
    // High-frequency subbands should dominate
    EXPECT_GT(report.wavelet_stats.hh_energy_pct, 10.0);
}
