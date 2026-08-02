#include "AnalyzeCmd.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/analyzer/Analyzer.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace xtm::cli {

int run_analyze(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: xtm analyze <input.tif> [--scale <value>]\n";
        return 1;
    }
    
    std::string input_file = argv[1];
    double scale = 1.0;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--scale" && i + 1 < argc) {
            scale = std::stod(argv[++i]);
        }
    }
    
    using namespace std::chrono;
    
    try {
        auto t0 = high_resolution_clock::now();
        std::cout << "Loading dataset: " << input_file << "...\n";
        auto buffer = io::read_gdal(input_file);
        auto t1 = high_resolution_clock::now();
        
        std::cout << "Analyzing terrain (scale=" << scale << ")...\n";
        auto report = analyzer::analyze_terrain(buffer.view(), scale, coding::ContextModel::Extended);
        auto t2 = high_resolution_clock::now();
        
        report.timing.gdal_ms = duration<double, std::milli>(t1 - t0).count();
        report.timing.analyze_ms = duration<double, std::milli>(t2 - t1).count();
        report.timing.total_ms = duration<double, std::milli>(t2 - t0).count();
        
        
        auto print_sep = []() { std::cout << std::string(60, '=') << "\n"; };
        auto print_header = [&](const std::string& title) {
            std::cout << "\n";
            print_sep();
            std::cout << "  " << title << "\n";
            print_sep();
        };
        
        print_header("Dataset Overview");
        std::cout << "Dimensions:       " << report.width << " x " << report.height << " (" << report.sample_count << " pixels)\n";
        std::cout << "Elevation Range:  [" << report.elevation.min_val << ", " << report.elevation.max_val << "]\n";
        std::cout << "Elevation Mean:   " << std::fixed << std::setprecision(2) << report.elevation.mean << "\n";
        std::cout << "Elevation StdDev: " << report.elevation.stddev << "\n";
        std::cout << "Unique Values:    " << report.elevation.unique_values << "\n";
        std::cout << "Shannon Entropy:  " << std::fixed << std::setprecision(4) << report.elevation.shannon_entropy << " bits/pixel\n";
        
        print_header("Spatial & Correlation Stats");
        std::cout << "Raw Correlation:\n";
        std::cout << "  Horizontal: " << std::setprecision(4) << report.correlation_stats.raw_horizontal << "\n";
        std::cout << "  Vertical:   " << report.correlation_stats.raw_vertical << "\n";
        std::cout << "  Diagonal:   " << report.correlation_stats.raw_diagonal << "\n";
        std::cout << "Residual Correlation:\n";
        std::cout << "  Horizontal: " << report.correlation_stats.residual_horizontal << "\n";
        std::cout << "  Vertical:   " << report.correlation_stats.residual_vertical << "\n";
        std::cout << "  Diagonal:   " << report.correlation_stats.residual_diagonal << "\n";
        
        print_header("Precision Analysis");
        std::cout << "Meter Entropy:      " << report.precision.meter_entropy << " bpp\n";
        std::cout << "Decimeter Entropy:  " << report.precision.decimeter_entropy << " bpp\n";
        std::cout << "Centimeter Entropy: " << report.precision.centimeter_entropy << " bpp\n";
        std::cout << "Millimeter Entropy: " << report.precision.millimeter_entropy << " bpp\n";

        print_header("Residual Distribution (Global Gradient)");
        std::cout << "Mean Absolute:      " << report.residual_dist_stats.mean_abs << "\n";
        std::cout << "Variance:           " << report.residual_dist_stats.variance << "\n";
        std::cout << "Zero Values (%):    " << std::setprecision(2) << report.residual_dist_stats.zero_pct << "%\n";
        std::cout << "Median Absolute:    " << report.residual_dist_stats.median << "\n";
        std::cout << "95th Percentile:    " << report.residual_dist_stats.p95 << "\n";
        std::cout << "99th Percentile:    " << report.residual_dist_stats.p99 << "\n";
        std::cout << "Max Absolute:       " << report.residual_dist_stats.max_val << "\n";
        
        print_header("Predictor Performance (Entropy bpp)");
        struct PredInfo {
            std::string name;
            double global_e;
            double local_e;
            double usage_pct;
            double avg_mag;
        };
        std::vector<PredInfo> preds;
        auto add_pred = [&](const std::string& name, double global_e, double local_e, double usage_pct, double avg_mag) {
            preds.push_back({name, global_e, local_e, usage_pct, avg_mag});
        };
        
        double total_usage = 0;
        total_usage += report.predictor_usage.left_count;
        total_usage += report.predictor_usage.above_count;
        total_usage += report.predictor_usage.average_count;
        total_usage += report.predictor_usage.gradient_count;
        total_usage += report.predictor_usage.jpegls_count;
        total_usage += report.predictor_usage.plane_count;
        total_usage += report.predictor_usage.gap_count;
        total_usage += report.predictor_usage.adaptive_gradient_count;
        total_usage += report.predictor_usage.least_squares_count;
        total_usage += report.predictor_usage.second_order_count;
        total_usage += report.predictor_usage.local_slope_count;
        if (total_usage == 0) total_usage = 1;
        
        auto usage_pct = [total_usage](size_t c) { return (double)c / total_usage * 100.0; };
        auto avg_mag = [](double sum, size_t count) { return count > 0 ? sum / count : 0.0; };
        
        add_pred("Left", report.global_predictors.left_entropy, report.block64_predictors.left_entropy, usage_pct(report.predictor_usage.left_count), avg_mag(report.predictor_usage.left_mag_sum, report.predictor_usage.left_count));
        add_pred("Above", report.global_predictors.above_entropy, report.block64_predictors.above_entropy, usage_pct(report.predictor_usage.above_count), avg_mag(report.predictor_usage.above_mag_sum, report.predictor_usage.above_count));
        add_pred("Average", report.global_predictors.average_entropy, report.block64_predictors.average_entropy, usage_pct(report.predictor_usage.average_count), avg_mag(report.predictor_usage.average_mag_sum, report.predictor_usage.average_count));
        add_pred("Gradient", report.global_predictors.gradient_entropy, report.block64_predictors.gradient_entropy, usage_pct(report.predictor_usage.gradient_count), avg_mag(report.predictor_usage.gradient_mag_sum, report.predictor_usage.gradient_count));
        add_pred("JPEG-LS", report.global_predictors.jpegls_entropy, report.block64_predictors.jpegls_entropy, usage_pct(report.predictor_usage.jpegls_count), avg_mag(report.predictor_usage.jpegls_mag_sum, report.predictor_usage.jpegls_count));
        add_pred("Plane", report.global_predictors.plane_entropy, report.block64_predictors.plane_entropy, usage_pct(report.predictor_usage.plane_count), avg_mag(report.predictor_usage.plane_mag_sum, report.predictor_usage.plane_count));
        add_pred("GAP (CALIC)", report.global_predictors.gap_entropy, report.block64_predictors.gap_entropy, usage_pct(report.predictor_usage.gap_count), avg_mag(report.predictor_usage.gap_mag_sum, report.predictor_usage.gap_count));
        add_pred("Adaptive Gradient", report.global_predictors.adaptive_gradient_entropy, report.block64_predictors.adaptive_gradient_entropy, usage_pct(report.predictor_usage.adaptive_gradient_count), avg_mag(report.predictor_usage.adaptive_gradient_mag_sum, report.predictor_usage.adaptive_gradient_count));
        add_pred("Least Squares", report.global_predictors.least_squares_entropy, report.block64_predictors.least_squares_entropy, usage_pct(report.predictor_usage.least_squares_count), avg_mag(report.predictor_usage.least_squares_mag_sum, report.predictor_usage.least_squares_count));
        add_pred("Second Order", report.global_predictors.second_order_entropy, report.block64_predictors.second_order_entropy, usage_pct(report.predictor_usage.second_order_count), avg_mag(report.predictor_usage.second_order_mag_sum, report.predictor_usage.second_order_count));
        add_pred("Local Slope", report.global_predictors.local_slope_entropy, report.block64_predictors.local_slope_entropy, usage_pct(report.predictor_usage.local_slope_count), avg_mag(report.predictor_usage.local_slope_mag_sum, report.predictor_usage.local_slope_count));
        
        std::sort(preds.begin(), preds.end(), [](const PredInfo& a, const PredInfo& b) {
            return a.global_e < b.global_e;
        });
        
        std::cout << std::left << std::setw(6) << "Rank" << std::setw(20) << "Predictor" 
                  << " | " << std::setw(8) << "Global" << " | " << std::setw(10) << "Local(64)" 
                  << " | " << std::setw(8) << "Usage" << " | " << "Avg Mag\n";
        std::cout << std::string(60, '-') << "\n";
        int rank = 1;
        for (const auto& p : preds) {
            std::cout << std::left << std::setw(6) << rank++ << std::setw(20) << p.name 
                      << " | " << std::fixed << std::setprecision(4) << std::setw(8) << p.global_e
                      << " | " << std::setw(10) << p.local_e
                      << " | " << std::setprecision(2) << std::setw(7) << p.usage_pct << "%"
                      << " | " << std::setprecision(2) << p.avg_mag << "\n";
        }
        
        print_header("Predictor Confidence (Best Predictor)");
        std::cout << "Exact (0):    " << std::fixed << std::setprecision(2) << report.predictor_confidence.pct_exact << "%\n";
        std::cout << "Within ±1:    " << report.predictor_confidence.pct_within_1 << "%\n";
        std::cout << "Within ±2:    " << report.predictor_confidence.pct_within_2 << "%\n";
        std::cout << "Within ±5:    " << report.predictor_confidence.pct_within_5 << "%\n";
        std::cout << "Within ±10:   " << report.predictor_confidence.pct_within_10 << "%\n";
        
        print_header("Prediction Difficulty");
        std::cout << "Easy   (<3 bpp):   " << std::fixed << std::setprecision(2) << report.prediction_difficulty.easy_pct << "%   (avg: " << report.prediction_difficulty.easy_avg_entropy << " bpp)\n";
        std::cout << "Medium (3-7 bpp):  " << report.prediction_difficulty.medium_pct << "%   (avg: " << report.prediction_difficulty.medium_avg_entropy << " bpp)\n";
        std::cout << "Hard   (>7 bpp):   " << report.prediction_difficulty.hard_pct << "%   (avg: " << report.prediction_difficulty.hard_avg_entropy << " bpp)\n";
        
        print_header("Residual Histogram");
        double r0=0, r1=0, r2_5=0, r6_10=0, r11_50=0, r51_100=0, r_more=0;
        for (const auto& kv : report.residual_histogram) {
            int32_t a = std::abs(kv.first);
            if (a == 0) r0 += kv.second;
            else if (a == 1) r1 += kv.second;
            else if (a <= 5) r2_5 += kv.second;
            else if (a <= 10) r6_10 += kv.second;
            else if (a <= 50) r11_50 += kv.second;
            else if (a <= 100) r51_100 += kv.second;
            else r_more += kv.second;
        }
        auto print_bar = [](const std::string& label, double pct) {
            std::cout << std::left << std::setw(12) << label << std::setw(7) << std::fixed << std::setprecision(2) << pct << "%  ";
            int bars = static_cast<int>(pct / 2.0);
            for(int i=0; i<bars; ++i) std::cout << "█";
            std::cout << "\n";
        };
        print_bar("|r|=0:", r0);
        print_bar("|r|=1:", r1);
        print_bar("|r|=2-5:", r2_5);
        print_bar("|r|=6-10:", r6_10);
        print_bar("|r|=11-50:", r11_50);
        print_bar("|r|=51-100:", r51_100);
        print_bar("|r|>100:", r_more);
        
        print_header("Quadtree Analysis");
        std::cout << "Total Leaves:       " << report.quadtree_leaves << "\n";
        std::cout << "512x512 Blocks:     " << report.quadtree_stats.size_512_count << "\n";
        std::cout << "256x256 Blocks:     " << report.quadtree_stats.size_256_count << "\n";
        std::cout << "128x128 Blocks:     " << report.quadtree_stats.size_128_count << "\n";
        std::cout << "64x64 Blocks:       " << report.quadtree_stats.size_64_count << "\n";
        
        print_header("Wavelet Analysis");
        std::cout << "Zero Runs (avg):    " << std::fixed << std::setprecision(2) << report.wavelet_stats.avg_zero_run 
                  << " (max: " << report.wavelet_stats.max_zero_run << ")\n";
        std::cout << "P95 / P99 Coeffs:   " << report.wavelet_stats.p95_coeff << " / " << report.wavelet_stats.p99_coeff << "\n";
        std::cout << "Subband Coefficient Counts:\n";
        std::cout << "  LL: " << report.wavelet_stats.ll_count << "  LH: " << report.wavelet_stats.lh_count 
                  << "  HL: " << report.wavelet_stats.hl_count << "  HH: " << report.wavelet_stats.hh_count << "\n";
        std::cout << "  Total: " << (report.wavelet_stats.ll_count + report.wavelet_stats.lh_count + report.wavelet_stats.hl_count + report.wavelet_stats.hh_count) << "\n";
        std::cout << "\nSubband Stats (LL | LH | HL | HH):\n";
        std::cout << "Entropy:     " << std::setprecision(4) << report.wavelet_stats.ll_entropy << " | " << report.wavelet_stats.lh_entropy << " | " << report.wavelet_stats.hl_entropy << " | " << report.wavelet_stats.hh_entropy << "\n";
        std::cout << "Zeros (%):   " << std::setprecision(2) << report.wavelet_stats.ll_zero_pct << "% | " << report.wavelet_stats.lh_zero_pct << "% | " << report.wavelet_stats.hl_zero_pct << "% | " << report.wavelet_stats.hh_zero_pct << "%\n";
        std::cout << "Energy (%):  " << std::setprecision(2) << report.wavelet_stats.ll_energy_pct << "% | " << report.wavelet_stats.lh_energy_pct << "% | " << report.wavelet_stats.hl_energy_pct << "% | " << report.wavelet_stats.hh_energy_pct << "%\n";
        
        print_header("Entropy Coding & Context Modeling");
        std::cout << "Avg Unique Contexts/Block: " << report.context_stats.unique_contexts << "\n";
        std::cout << "Avg Symbols per Context:   " << std::fixed << std::setprecision(2) << report.context_stats.avg_symbols_per_context << "\n";
        std::cout << "Context Size Range:        [" << report.context_stats.smallest_context << ", " << report.context_stats.largest_context << "] (median: " << report.context_stats.median_context_size << ")\n";
        
        print_header("Information Reduction Pipeline");
        std::cout << std::left << std::setw(22) << "Stage" << std::setw(15) << "Entropy(bpp)" << std::setw(15) << "Δ(bpp)" << std::setw(15) << "Reduction%" << "Cumulative%\n";
        std::cout << std::string(75, '-') << "\n";
        
        double e0 = report.elevation.shannon_entropy;
        double e1 = report.adaptive_block64_entropy;
        double e2 = report.quadtree_entropy;
        double e3 = report.dwt_quadtree_entropy;
        
        auto print_stage = [e0](const std::string& name, double e, double prev_e) {
            double delta = e - prev_e;
            double red_pct = prev_e > 0 ? -(delta / prev_e * 100.0) : 0;
            double cum_pct = e0 > 0 ? ((e0 - e) / e0 * 100.0) : 0;
            std::cout << std::left << std::setw(22) << name << std::fixed << std::setprecision(4) << std::setw(15) << e 
                      << std::showpos << std::setw(15) << delta << std::noshowpos << std::setw(15) << (std::to_string(red_pct) + "%") 
                      << (std::to_string(cum_pct) + "%") << "\n";
        };
        
        std::cout << std::left << std::setw(22) << "Raw Elevation" << std::fixed << std::setprecision(4) << std::setw(15) << e0 << std::setw(15) << "—" << std::setw(15) << "—" << "—\n";
        print_stage("Adaptive Prediction", e1, e0);
        print_stage("Quadtree Adaptive", e2, e1);
        print_stage("DWT + Quadtree", e3, e2);
        
        print_header("Wavelet Heuristic Evaluation");
        std::cout << "Predicted DWT Gain: " << std::setprecision(4) << report.transform_eval_stats.predicted_gain << " bpp\n";
        std::cout << "Actual DWT Benefit: " << report.transform_eval_stats.actual_wavelet_benefit << " bpp\n";
        std::cout << "Prediction Error:   " << report.transform_eval_stats.prediction_error << " bpp\n";
        std::cout << "Heuristic Decision: " << (report.transform_eval_stats.decision_use_wavelet ? "Wavelet Enabled" : "Wavelet Disabled") << "\n";
        std::cout << "Accuracy:           " << (report.transform_eval_stats.prediction_correct ? "Correct" : "Incorrect") << "\n";
        
        print_header("Compute Analysis");
        std::cout << "GDAL Load Time:      " << std::fixed << std::setprecision(2) << report.timing.gdal_ms << " ms\n";
        std::cout << "Analysis Time:       " << report.timing.analyze_ms << " ms\n";
        std::cout << "Total Time:          " << report.timing.total_ms << " ms\n";
        print_sep();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

} // namespace xtm::cli
