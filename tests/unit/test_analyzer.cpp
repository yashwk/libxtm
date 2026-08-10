#include <gtest/gtest.h>
#include "xtm/analyzer/Analyzer.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include <vector>
#include <random>
#include <cmath>
#include <functional>

using namespace xtm;
using namespace xtm::analyzer;

namespace {

terrain::IntGrid make_grid(uint32_t w, uint32_t h, const std::function<int32_t(uint32_t, uint32_t)>& fn) {
    terrain::IntGrid g;
    g.width = w;
    g.height = h;
    g.data.resize(static_cast<std::size_t>(w) * h);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            g.data[y * w + x] = fn(x, y);
        }
    }
    return g;
}

void assert_invariants(const AnalysisReport& rep) {
    // All 6 predictors are reported.
    EXPECT_EQ(rep.predictors.size(), 6u);

    // Table is ranked by selection bpp.
    for (std::size_t i = 1; i < rep.predictors.size(); ++i) {
        EXPECT_LE(rep.predictors[i - 1].selection_bpp, rep.predictors[i].selection_bpp);
    }

    // bpp values are sane.
    EXPECT_GE(rep.budget.total_bpp, 0.0);
    EXPECT_GE(rep.budget.overhead_bpp, 0.0);
    EXPECT_GE(rep.predictor_estimate_bpp, 0.0);

    // Quadtree leaf counts cover the grid partition.
    std::size_t total = rep.leaves_512 + rep.leaves_256 + rep.leaves_128 + rep.leaves_64;
    EXPECT_EQ(total, rep.total_blocks);
    EXPECT_GT(rep.total_blocks, 0u);

    // Correlation in [-1, 1].
    EXPECT_GE(rep.corr_h, -1.01);
    EXPECT_LE(rep.corr_h, 1.01);
    EXPECT_GE(rep.corr_v, -1.01);
    EXPECT_LE(rep.corr_v, 1.01);

    // Estimated file size is at least the container overhead.
    EXPECT_GT(rep.estimated_file_bytes, 256.0);

    // Every predictor was exercised on the whole superblocks.
    for (const auto& p : rep.predictors) {
        EXPECT_GE(p.shannon_bpp, 0.0);
    }
}

} // namespace

TEST(AnalyzerTest, ConstantPlane) {
    auto grid = make_grid(1024, 1024, [](uint32_t, uint32_t) { return 1000; });
    coding::PipelineContext ctx(1.0);
    auto report = analyze_terrain(grid, RawElevationStats{}, ctx);

    assert_invariants(report);

    EXPECT_NEAR(report.corr_h, 1.0, 1e-4);

    // Constant grid: quadtree selection falls back to Left and residuals are
    // zero everywhere except the first column (no left neighbor), which costs
    // ~0.02 bpp. The entropy budget should still be dominated by overhead.
    EXPECT_EQ(report.chosen_predictor, predictor::PredictorId::Left);
    EXPECT_NEAR(report.chosen_usage_pct, 100.0, 1e-6);

    EXPECT_LT(report.budget.magnitude_class_bpp, 0.05);
    EXPECT_LT(report.budget.zero_run_bpp, 0.05);
    EXPECT_LT(report.budget.remainder_bpp, 0.05);
    EXPECT_LT(report.budget.total_bpp, 0.1);

    EXPECT_LT(report.predictors[0].shannon_bpp, 0.05);
}

TEST(AnalyzerTest, LinearRamp) {
    auto grid = make_grid(512, 512, [](uint32_t x, uint32_t y) { return static_cast<int32_t>(x + y); });
    coding::PipelineContext ctx(1.0);
    auto report = analyze_terrain(grid, RawElevationStats{}, ctx);

    assert_invariants(report);

    // Linear ramp: residuals are ~1, so true residual entropy is near zero.
    // Left or Gradient wins; the first column (no left neighbor) costs ~0.02
    // bpp, so the low-selection-bpp invariant is the meaningful one.
    EXPECT_LT(report.predictors[0].selection_bpp, 0.1);
    EXPECT_LT(report.budget.magnitude_class_bpp, 0.05);
    EXPECT_EQ(report.sample_count, 512u * 512u);
}

TEST(AnalyzerTest, RandomNoise) {
    auto grid = make_grid(512, 512, [](uint32_t x, uint32_t y) {
        std::mt19937 gen(42u + x * 7919u + y * 104729u);
        std::uniform_int_distribution<int32_t> dis(0, 1000);
        return dis(gen);
    });
    coding::PipelineContext ctx(1.0);
    auto report = analyze_terrain(grid, RawElevationStats{}, ctx);

    assert_invariants(report);

    // Noise: predictors struggle, correlation collapses, cost is high.
    EXPECT_LT(report.corr_h, 0.1);
    EXPECT_LT(report.corr_v, 0.1);
    EXPECT_GT(report.budget.total_bpp, 5.0);
    EXPECT_GT(report.predictors[0].selection_bpp, 1.0);

    // Nodata accounting: no mask means zero nodata pixels.
    EXPECT_EQ(report.nodata_pixels, 0u);
}

TEST(AnalyzerTest, DigitPlanes) {
    // 0.01 precision (multiplier 100): units = cm, tens = dm, hundreds = m.
    auto grid = make_grid(1024, 1024, [](uint32_t x, uint32_t y) { return 12345 + static_cast<int32_t>(x) % 10 + 100 * static_cast<int32_t>(y) % 100; });
    coding::PipelineContext ctx(0.01);
    auto report = analyze_terrain(grid, RawElevationStats{}, ctx);

    assert_invariants(report);

    // Multiplier 100 -> 3 digit planes (units, tens, hundreds).
    ASSERT_EQ(report.digit_planes.size(), 3u);
    EXPECT_EQ(report.digit_planes[0].place, 0);
    EXPECT_EQ(report.digit_planes[1].place, 1);
    EXPECT_EQ(report.digit_planes[2].place, 2);
    EXPECT_GE(report.digit_planes[0].bpp, 0.0);
    EXPECT_LE(report.digit_planes[0].bpp, 10.0 * std::log2(10.0));

    // Split-precision context flows into per-block overhead (extra prec pid byte).
    EXPECT_GT(report.budget.overhead_bpp, 0.0);
}

TEST(AnalyzerTest, WaveletEvaluation) {
    auto grid = make_grid(512, 512, [](uint32_t x, uint32_t y) { return static_cast<int32_t>(x + y); });
    coding::PipelineContext ctx(1.0);
    AnalyzerOptions options;
    options.enable_wavelet_analysis = true;
    auto report = analyze_terrain(grid, RawElevationStats{}, ctx, options);

    assert_invariants(report);

    EXPECT_TRUE(report.wavelet_evaluated);
    EXPECT_GE(report.wavelet_estimate_bpp, 0.0);

    // Without the flag, no wavelet evaluation happens.
    AnalyzerOptions no_options;
    auto report2 = analyze_terrain(grid, RawElevationStats{}, ctx, no_options);
    EXPECT_FALSE(report2.wavelet_evaluated);
}
