#pragma once
#include "xtm/Terrain.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include <map>
#include <vector>

namespace xtm::analyzer {

struct ElevationStats {
    float min_val;
    float max_val;
    double mean;
    double stddev;
    std::size_t unique_values;
    double shannon_entropy;
};

struct PrecisionStats {
    double meter_entropy = 0;
    double decimeter_entropy = 0;
    double centimeter_entropy = 0;
    double millimeter_entropy = 0;
};

struct SpatialDifferences {
    double delta_x_entropy;
    double delta_y_entropy;
};

struct PredictorPerformance {
    double left_entropy;
    double above_entropy;
    double average_entropy;
    double gradient_entropy;
    double jpegls_entropy;
    double plane_entropy;
    double gap_entropy;
    double adaptive_gradient_entropy;
    double least_squares_entropy;
    double second_order_entropy;
    double local_slope_entropy;
};

struct PredictorConfidence {
    double pct_exact = 0;
    double pct_within_1 = 0;
    double pct_within_2 = 0;
    double pct_within_5 = 0;
    double pct_within_10 = 0;
};

struct PredictionDifficulty {
    double easy_pct = 0;
    double medium_pct = 0;
    double hard_pct = 0;
    double easy_avg_entropy = 0;
    double medium_avg_entropy = 0;
    double hard_avg_entropy = 0;
};

struct PredictorUsage {
    std::size_t left_count = 0;
    std::size_t above_count = 0;
    std::size_t average_count = 0;
    std::size_t gradient_count = 0;
    std::size_t jpegls_count = 0;
    std::size_t plane_count = 0;
    std::size_t gap_count = 0;
    std::size_t adaptive_gradient_count = 0;
    std::size_t least_squares_count = 0;
    std::size_t second_order_count = 0;
    std::size_t local_slope_count = 0;
    
    double left_mag_sum = 0;
    double above_mag_sum = 0;
    double average_mag_sum = 0;
    double gradient_mag_sum = 0;
    double jpegls_mag_sum = 0;
    double plane_mag_sum = 0;
    double gap_mag_sum = 0;
    double adaptive_gradient_mag_sum = 0;
    double least_squares_mag_sum = 0;
    double second_order_mag_sum = 0;
    double local_slope_mag_sum = 0;
};

struct QuadtreeStats {
    std::size_t size_512_count = 0;
    std::size_t size_256_count = 0;
    std::size_t size_128_count = 0;
    std::size_t size_64_count = 0;
    std::size_t max_depth = 0;
    double avg_depth = 0;
};

struct SubbandStats {
    double ll_entropy = 0;
    double lh_entropy = 0;
    double hl_entropy = 0;
    double hh_entropy = 0;
    
    double ll_zero_pct = 0;
    double lh_zero_pct = 0;
    double hl_zero_pct = 0;
    double hh_zero_pct = 0;
    
    double ll_mean_mag = 0;
    double lh_mean_mag = 0;
    double hl_mean_mag = 0;
    double hh_mean_mag = 0;
    
    double ll_var = 0;
    double lh_var = 0;
    double hl_var = 0;
    double hh_var = 0;
    
    double ll_energy_pct = 0;
    double lh_energy_pct = 0;
    double hl_energy_pct = 0;
    double hh_energy_pct = 0;
    
    double avg_zero_run = 0;
    std::size_t max_zero_run = 0;
    std::size_t ll_count = 0;
    std::size_t lh_count = 0;
    std::size_t hl_count = 0;
    std::size_t hh_count = 0;
    int32_t p95_coeff = 0;
    int32_t p99_coeff = 0;
};

struct ContextStats {
    std::size_t unique_contexts = 0;
    double avg_symbols_per_context = 0;
    std::size_t largest_context = 0;
    std::size_t smallest_context = 0;
    std::size_t median_context_size = 0;
    std::size_t probability_rescales = 0;
};

struct CorrelationStats {
    double raw_horizontal = 0;
    double raw_vertical = 0;
    double raw_diagonal = 0;
    
    double residual_horizontal = 0;
    double residual_vertical = 0;
    double residual_diagonal = 0;
};

struct ResidualDistributionStats {
    double mean_abs = 0;
    double variance = 0;
    double zero_pct = 0;
    int32_t median = 0;
    int32_t p95 = 0;
    int32_t p99 = 0;
    int32_t max_val = 0;
};

struct TransformEvaluationStats {
    double estimated_wavelet_benefit = 0;
    double actual_wavelet_benefit = 0;
    bool decision_use_wavelet = false;
    bool prediction_correct = false;
    double predicted_gain = 0;
    double prediction_error = 0;
};

struct TimingStats {
    double gdal_ms = 0;
    double quantization_ms = 0;
    double analyze_ms = 0;
    double total_ms = 0;
};

struct AnalysisReport {
    std::uint32_t width;
    std::uint32_t height;
    std::size_t sample_count;
    
    ElevationStats elevation;
    PrecisionStats precision;
    SpatialDifferences spatial;
    PredictorPerformance global_predictors;
    PredictorPerformance block64_predictors;
    
    double adaptive_block64_entropy;
    double quadtree_entropy;
    std::size_t quadtree_leaves;
    
    double dwt_quadtree_entropy;
    
    PredictorUsage predictor_usage;
    PredictorConfidence predictor_confidence;
    PredictionDifficulty prediction_difficulty;
    std::vector<std::pair<int32_t, double>> residual_histogram;
    QuadtreeStats quadtree_stats;
    SubbandStats wavelet_stats;
    ContextStats context_stats;
    CorrelationStats correlation_stats;
    ResidualDistributionStats residual_dist_stats;
    TransformEvaluationStats transform_eval_stats;
    TimingStats timing;
};

AnalysisReport analyze_terrain(const TerrainView& view, double scale = 1.0, coding::ContextModel model = coding::ContextModel::Extended);

} // namespace xtm::analyzer
