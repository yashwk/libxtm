#pragma once
#include "xtm/Terrain.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include "xtm/predictor/Predictor.hpp"
#include <array>
#include <string>
#include <vector>
#include <cstdint>

namespace xtm::analyzer {

// Raw elevation statistics of the original (unquantized) raster. Accumulated
// by the reader during the windowed load; nodata cells are excluded.
struct RawElevationStats {
    double min_val = 0.0;
    double max_val = 0.0;
    double mean = 0.0;
    double stddev = 0.0;
    std::size_t valid_pixels = 0;

    // p1/p25/p50/p75/p99 estimated from a deterministic stride sample.
    std::array<double, 5> percentiles = {0.0, 0.0, 0.0, 0.0, 0.0};
    // 50-bucket elevation distribution over [min_val, max_val], normalized
    // to [0, 1] by the peak bucket; empty when no valid pixels.
    std::vector<double> elevation_histogram;
};

// Statistics of the quantized grid, i.e. the values the encoder compresses.
struct QuantizedStats {
    double min_val = 0.0;
    double max_val = 0.0;
    double mean = 0.0;
    double stddev = 0.0;
};

// Per-predictor results from encoding every whole 512x512 superblock with
// that predictor alone. selection_bpp uses the encoder's own scoring
// (8-bit id + 32 bits/parameter + zigzag-model estimate), shannon_bpp is the
// true residual entropy.
struct PredictorPerformance {
    predictor::PredictorId id = predictor::PredictorId::Left;
    const char* name = "";
    double selection_bpp = 0.0;
    double shannon_bpp = 0.0;
    std::size_t usage_blocks = 0;
    double avg_abs_residual = 0.0;
};

// Where the encoder's estimated bit budget goes. Mirrors the selection cost
// model (zigzag classes + zero runs + remainder bits) plus the fixed per-block
// and quadtree overhead of the container.
struct EntropyBudget {
    double magnitude_class_bpp = 0.0;
    double zero_run_bpp = 0.0;
    double remainder_bpp = 0.0;
    double params_bpp = 0.0;
    double overhead_bpp = 0.0;
    double total_bpp = 0.0;
};

// Shannon entropy of one decimal digit plane of the quantized magnitudes
// (place 0 = units digit = the finest plane present). Informational: digits
// are correlated, so they cannot be added up to predict coarser-precision
// costs - use precision_estimates for that.
struct DigitPlaneEntropy {
    int place = 0; // 0 = units, 1 = tens, ...
    double bpp = 0.0;
};

// Estimated cost of encoding at a given precision, computed by re-running the
// encoder's selection pass on grids derived from the quantized data (values
// divided by 10^k). Only present for power-of-ten precisions < 1.
struct PrecisionEstimate {
    double precision = 0.0;
    double bpp = 0.0;
    double estimated_file_bytes = 0.0;
};

struct AnalyzerOptions {
    bool enable_wavelet_analysis = false;
};

struct AnalysisReport {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t sample_count = 0;
    std::size_t nodata_pixels = 0;
    double precision = 1.0;

    RawElevationStats raw;
    QuantizedStats quantized;

    double corr_h = 0.0;
    double corr_v = 0.0;
    double corr_d = 0.0;

    // Digit-plane entropies (finer first); empty when the precision is not a
    // power of ten.
    std::vector<DigitPlaneEntropy> digit_planes;

    // Estimated size at the current precision followed by each 10x coarser
    // precision derivable from the grid (finest first). Empty when the
    // precision is not a power of ten below 1.0.
    std::vector<PrecisionEstimate> precision_estimates;

    // All 6 predictors, ranked by selection_bpp.
    std::vector<PredictorPerformance> predictors;
    predictor::PredictorId chosen_predictor = predictor::PredictorId::Left;
    double chosen_usage_pct = 0.0;
    double second_order_usage_pct = 0.0;

    // Residual re-prediction pool: blocks won by each of the 7 ResidualPredictorIds
    // (0 = None, 1 = Average, 2 = Median, 3 = Left, 4 = Gradient, 5 = Gap,
    // 6 = LeastSquares) and the estimated bits saved by the pool over the
    // plain primary residuals (sum of per-block second_order_bits_savings).
    std::array<std::size_t, 7> residual_predictor_blocks = {};
    double residual_pool_savings_bits = 0.0;
    double second_order_savings_bpp = 0.0;

    std::size_t leaves_512 = 0;
    std::size_t leaves_256 = 0;
    std::size_t leaves_128 = 0;
    std::size_t leaves_64 = 0;
    std::size_t total_blocks = 0;

    EntropyBudget budget;
    double estimated_file_bytes = 0.0;
    // Spread of the size estimate: one stddev of the per-leaf selection-bit
    // budget propagated to total payload bytes (blocks assumed independent).
    double estimated_bytes_stddev = 0.0;
    // Raw Float32 bytes (sample_count * 4) / estimated_file_bytes.
    double estimated_compression_ratio = 0.0;

    // Georeferencing surfaced from the dataset header (absent when unset).
    std::string crs;
    std::string pixel_units;
    bool has_georeference = false;
    double pixel_width = 0.0;
    double pixel_height = 0.0;
    double bbox_min_x = 0.0;
    double bbox_min_y = 0.0;
    double bbox_max_x = 0.0;
    double bbox_max_y = 0.0;

    // Per-phase wall time, accumulated across superblocks (parallel workers
    // overlap, so the sum may exceed the wall-clock total).
    double time_quadtree_ms = 0.0;
    double time_predictor_eval_ms = 0.0;
    double time_entropy_ms = 0.0;

    bool wavelet_evaluated = false;
    double predictor_estimate_bpp = 0.0;
    double wavelet_estimate_bpp = 0.0;
    bool wavelet_recommended = false;
};

// Analyzes the quantized grid with the same partitioner and selector the
// encoder uses. All aggregates are reduced serially in superblock order, so
// the report is deterministic for a given grid and settings.
AnalysisReport analyze_terrain(const terrain::IntGrid& grid,
                               const RawElevationStats& raw,
                               const coding::PipelineContext& ctx,
                               const AnalyzerOptions& options = AnalyzerOptions());

} // namespace xtm::analyzer
