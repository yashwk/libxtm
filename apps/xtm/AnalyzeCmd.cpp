#include "AnalyzeCmd.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/analyzer/Analyzer.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace xtm::cli {

void print_usage() {
    std::cerr << "Usage: xtm analyze <input_raster> [options]\n";
    std::cerr << "Options:\n";
    std::cerr << "  --wavelet, -w       Evaluate the DWT pipeline against the predictor pipeline\n";
    std::cerr << "  --precision <value> Precision factor for analysis (default 1.0)\n";
}
double parse_precision(const std::string& arg);

int run_analyze(int argc, char** argv) {
    std::string input_path;
    double precision = 1.0;
    bool enable_wavelet = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--wavelet" || arg == "-w") {
            enable_wavelet = true;
        } else if ((arg == "--scale" || arg == "--precision") && i + 1 < argc) {
            precision = parse_precision(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (input_path.empty() && arg[0] != '-') {
            input_path = arg;
        }
    }
    
    if (input_path.empty()) {
        print_usage();
        return 1;
    }
    
    using namespace std::chrono;
    
    try {
        auto t0 = high_resolution_clock::now();
        std::cout << "Loading dataset: " << input_path << "...\n";
        io::RasterInfo info;
        auto grid = io::read_gdal_quantized(input_path, precision, info);
        auto t1 = high_resolution_clock::now();

        analyzer::AnalyzerOptions options;
        options.enable_wavelet_analysis = enable_wavelet;

        std::cout << "Analyzing terrain (precision=" << precision << ")...\n";
        coding::PipelineContext ctx(precision);
        analyzer::RawElevationStats raw;
        raw.min_val = info.raw_min;
        raw.max_val = info.raw_max;
        raw.mean = info.raw_mean;
        raw.stddev = info.raw_stddev;
        raw.valid_pixels = info.raw_valid_pixels;
        auto report = analyzer::analyze_terrain(grid, raw, ctx, options);
        auto t2 = high_resolution_clock::now();
        
        double load_ms = duration<double, std::milli>(t1 - t0).count();
        double analyze_ms = duration<double, std::milli>(t2 - t1).count();
        
        auto print_sep = []() { std::cout << std::string(60, '=') << "\n"; };
        auto print_header = [&](const std::string& title) {
            std::cout << "\n";
            print_sep();
            std::cout << "  " << title << "\n";
            print_sep();
        };
        
        // ---- 1. Dataset Overview ----
        print_header("Dataset Overview");
        std::cout << "Dimensions:          " << report.width << " x " << report.height
                  << " (" << report.sample_count << " pixels)\n";
        std::cout << "NoData pixels:       " << report.nodata_pixels;
        if (report.sample_count > 0) {
            std::cout << " (" << std::fixed << std::setprecision(2)
                      << 100.0 * report.nodata_pixels / report.sample_count << "%)";
        }
        std::cout << "\n";
        std::cout << "Raw Elevation:       [" << std::fixed << std::setprecision(2) << report.raw.min_val
                  << ", " << report.raw.max_val << "]"
                  << "  mean: " << report.raw.mean
                  << "  stddev: " << report.raw.stddev
                  << "  (" << report.raw.valid_pixels << " valid)\n";
        std::cout << "Quantized @ " << report.precision << ": [" << std::setprecision(2)
                  << report.quantized.min_val << ", " << report.quantized.max_val << "]"
                  << "  mean: " << report.quantized.mean
                  << "  stddev: " << report.quantized.stddev << "\n";
        std::cout << "Spatial correlation: H: " << std::setprecision(4) << report.corr_h
                  << "  V: " << report.corr_v
                  << "  D: " << report.corr_d << "\n";

        // ---- 2. Compressibility & Precision Guidance ----
        print_header("Compressibility & Precision Guidance");
        std::cout << "Estimated coding cost:  " << std::fixed << std::setprecision(4)
                  << report.budget.total_bpp << " bpp at precision " << report.precision << "\n";
        double mb = report.estimated_file_bytes / (1024.0 * 1024.0);
        std::cout << "Estimated file size:    " << std::setprecision(2) << mb
                  << " MB (incl. header + block index)\n";

        if (report.precision_estimates.size() > 1) {
            std::cout << "\nPrecision options (re-running the encoder's selection on coarser grids):\n";
            std::cout << std::left << std::setw(12) << "Precision"
                      << std::setw(12) << "Est. bpp"
                      << std::setw(14) << "Est. size"
                      << "vs finest\n";
            std::cout << std::string(60, '-') << "\n";
            for (std::size_t k = 0; k < report.precision_estimates.size(); ++k) {
                const auto& pe = report.precision_estimates[k];
                std::ostringstream size_str;
                size_str << std::fixed << std::setprecision(2)
                         << pe.estimated_file_bytes / (1024.0 * 1024.0) << " MB";
                std::string note;
                if (k > 0) {
                    double save = report.precision_estimates[0].bpp - pe.bpp;
                    std::ostringstream ns;
                    ns << "saves " << std::fixed << std::setprecision(2) << save << " bpp";
                    note = ns.str();
                } else {
                    note = "(current)";
                }
                std::cout << std::left << std::setw(12) << pe.precision
                          << std::fixed << std::setprecision(2) << std::setw(12) << pe.bpp
                          << std::setw(14) << size_str.str()
                          << note << "\n";
            }
            std::cout << "  Precision is 10x coarser per row; the digits' entropy below shows where the detail lives.\n";
        }

        if (!report.digit_planes.empty()) {
            std::cout << "\nDigit-plane entropies (information per decimal digit of the quantized values):\n";
            auto plane_label = [&](int place) {
                double unit = report.precision;
                for (int k = 0; k < place; ++k) unit *= 10.0;
                std::ostringstream os;
                os << std::fixed << std::setprecision(unit >= 1.0 ? 0 : (unit < 0.01 ? 4 : 2)) << unit << " m";
                return os.str();
            };
            std::cout << std::left << std::setw(14) << "Digit plane" << std::setw(12) << "Entropy\n";
            std::cout << std::string(40, '-') << "\n";
            for (const auto& dp : report.digit_planes) {
                std::cout << std::left << std::setw(14) << plane_label(dp.place)
                          << std::fixed << std::setprecision(4) << dp.bpp << "\n";
            }
            if (report.digit_planes.size() == 1) {
                std::cout << "  Re-run with --precision 0.1/0.01 to measure finer planes.\n";
            }
        }

        // ---- 3. Predictor Analysis ----
        print_header("Predictor Analysis (whole 512x512 superblocks)");
        std::cout << std::left << std::setw(6) << "Rank"
                  << std::setw(16) << "Predictor"
                  << std::setw(16) << "Selection bpp"
                  << std::setw(13) << "Shannon bpp"
                  << std::setw(10) << "Usage"
                  << "Avg |res|\n";
        std::cout << "  Selection bpp = encoder cost model (id + params + zigzag estimate); Shannon bpp = true residual entropy\n";
        std::cout << std::string(70, '-') << "\n";
        int rank = 1;
        for (const auto& p : report.predictors) {
            std::ostringstream usage_str;
            usage_str << std::fixed << std::setprecision(1)
                      << (report.total_blocks > 0 ? 100.0 * p.usage_blocks / report.total_blocks : 0.0)
                      << "%";
            std::cout << std::left << std::setw(6) << rank++
                      << std::setw(16) << p.name
                      << std::fixed << std::setprecision(4) << std::setw(16) << p.selection_bpp
                      << std::setw(13) << p.shannon_bpp
                      << std::setw(10) << usage_str.str()
                      << std::setprecision(2) << p.avg_abs_residual << "\n";
        }
        const char* chosen_name = "";
        for (const auto& p : report.predictors) {
            if (p.id == report.chosen_predictor) chosen_name = p.name;
        }
        std::cout << "Encoder will choose: " << chosen_name << " (quadtree winner, used on "
                  << std::fixed << std::setprecision(1) << report.chosen_usage_pct << "% of blocks)\n";
        std::cout << "Second-order pass:   triggered on " << std::setprecision(1)
                  << report.second_order_usage_pct << "% of blocks\n";

        // ---- 4. Quadtree Analysis ----
        print_header("Quadtree Analysis");
        std::cout << "Leaves:  512x512: " << report.leaves_512
                  << "  256x256: " << report.leaves_256
                  << "  128x128: " << report.leaves_128
                  << "  64x64:   " << report.leaves_64 << "\n";
        std::cout << "Total blocks:        " << report.total_blocks << "\n";
        if (report.total_blocks > 0) {
            double avg_px = static_cast<double>(report.sample_count) / report.total_blocks;
            std::cout << "Avg block area:      " << std::fixed << std::setprecision(1) << avg_px
                      << " px (" << std::setprecision(0) << std::sqrt(avg_px) << "x" 
                      << std::sqrt(avg_px) << ")\n";
        }
        std::cout << "Partition + header overhead: " << std::setprecision(4)
                  << report.budget.overhead_bpp << " bpp\n";

        // ---- 5. Entropy Budget ----
        print_header("Entropy Budget (estimated, mirrors encoder cost model)");
        std::cout << "Magnitude classes:  " << std::fixed << std::setprecision(4)
                  << report.budget.magnitude_class_bpp << " bpp\n";
        std::cout << "Zero runs:          " << report.budget.zero_run_bpp << " bpp\n";
        std::cout << "Remainder bits:     " << report.budget.remainder_bpp << " bpp\n";
        std::cout << "Parameters:         " << report.budget.params_bpp << " bpp\n";
        std::cout << "Overhead:           " << report.budget.overhead_bpp << " bpp\n";
        std::cout << "TOTAL:              " << report.budget.total_bpp << " bpp\n";

        // ---- 6. Wavelet Evaluation ----
        if (report.wavelet_evaluated) {
            print_header("Wavelet Evaluation");
            std::cout << "Predictor estimate: " << std::fixed << std::setprecision(4)
                      << report.predictor_estimate_bpp << " bpp\n";
            std::cout << "DWT estimate:       " << report.wavelet_estimate_bpp << " bpp\n";
            double gain = report.predictor_estimate_bpp - report.wavelet_estimate_bpp;
            std::cout << "Wavelet gain:       " << std::showpos << gain << std::noshowpos << " bpp\n";
            std::cout << "Recommendation:     " << (report.wavelet_recommended ? "use the wavelet pipeline" : "stay with the predictor pipeline") << "\n";
        }

        // ---- 7. Summary ----
        print_header("Summary");
        std::cout << "Estimated size at precision " << report.precision << ": "
                  << std::setprecision(2) << mb << " MB (" << std::setprecision(4)
                  << report.budget.total_bpp << " bpp)\n";
        std::cout << "Recommended precision: ";
        if (report.precision_estimates.size() > 1) {
            const auto& finest = report.precision_estimates[0];
            std::size_t best = 1;
            double best_save_per_cost = 0.0;
            for (std::size_t k = 1; k < report.precision_estimates.size(); ++k) {
                double save = finest.bpp - report.precision_estimates[k].bpp;
                if (save > best_save_per_cost) {
                    best_save_per_cost = save;
                    best = k;
                }
            }
            const auto& pe = report.precision_estimates[best];
            std::cout << "the biggest 10x step saves " << std::setprecision(2)
                      << best_save_per_cost << " bpp (" << pe.precision
                      << " m -> ~" << pe.estimated_file_bytes / (1024.0 * 1024.0) << " MB)\n";
        } else {
            std::cout << "current precision only; re-run with --precision < 1 to evaluate finer detail\n";
        }
        if (report.wavelet_evaluated) {
            std::cout << "Recommended pipeline: "
                      << (report.wavelet_recommended ? "wavelet" : "predictor") << "\n";
        }
        if (report.corr_h > 0.9 && report.corr_v > 0.9) {
            std::cout << "Terrain character:   smooth and highly correlated; predictors should perform well\n";
        } else if (report.corr_h < 0.3 || report.corr_v < 0.3) {
            std::cout << "Terrain character:   noisy/low-correlation; expect modest compression\n";
        }
        std::cout << "Compute:             load " << std::fixed << std::setprecision(0) << load_ms
                  << " ms, analysis " << analyze_ms << " ms\n";
        print_sep();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

} // namespace xtm::cli
