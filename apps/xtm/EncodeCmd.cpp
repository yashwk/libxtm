#include "EncodeCmd.hpp"
#include "xtm/Api.hpp"
#include "xtm/predictor/Predictors.hpp"
#include <iostream>
#include <string>
#include <map>
#include <iomanip>
#include <vector>
#include <algorithm>

namespace xtm::cli {

double parse_precision(const std::string& arg) {
    if (arg == "m" || arg == "meters" || arg == "meter") return 1.0;
    if (arg == "dm" || arg == "decimeter" || arg == "decimeters") return 0.1;
    if (arg == "cm" || arg == "centimeter" || arg == "centimeters") return 0.01;
    if (arg == "mm" || arg == "millimeter" || arg == "millimeters") return 0.001;
    try {
        return std::stod(arg);
    } catch (const std::exception&) {
        return -1.0;
    }
}

void print_pipeline_statistics(const api::EncodeResult& result, analyzer::PipelineType pipeline_type) {
    if (pipeline_type == analyzer::PipelineType::Wavelet) {
        std::cout << "\n=== Wavelet Statistics ===\n";
        if (result.total_blocks > 0) {
            std::cout << "  Wavelet Pipeline applied globally to " << result.total_blocks << " blocks.\n";
        } else {
            std::cout << "  No blocks processed.\n";
        }
        std::cout << "==========================\n";
        return;
    }

    std::cout << "\n=== Predictor Statistics ===\n";
    if (result.total_blocks == 0) {
        std::cout << "  No blocks processed.\n";
        std::cout << "============================\n";
        return;
    }

    predictor::PredictorBank bank;
    std::vector<const predictor::Predictor*> predictors_list = bank.ordered();

    struct Stat {
        uint32_t id;
        std::string name;
        uint32_t count;
    };

    std::vector<Stat> stats;
    for (const auto& kv : result.predictor_counts) {
        if (kv.second == 0) continue;
        std::string name = "Unknown";
        if (kv.first < predictors_list.size()) {
            name = predictors_list[kv.first]->name();
        }
        stats.push_back({
            kv.first, name, kv.second
        });
    }

    // Sort by most used predictors descending
    std::sort(stats.begin(), stats.end(), [](const Stat& a, const Stat& b) {
        return a.count > b.count;
    });

    for (const auto& s : stats) {
        double pct = (double)s.count / result.total_blocks * 100.0;

        std::cout << "- " << s.name << ":\n"
                  << "    Usage:              " << std::fixed << std::setprecision(2) << pct << "%\n";

        // Machine-readable summary consumed by utils/benchmark_suite.py
        std::cout << "Predictor " << s.id << ": " << s.count << " blocks (" << pct << "%)\n";
    }
    std::cout << "============================\n";
}

int run_encode(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: xtm encode <input.tif> -o <output.xtm> [--precision <value>] [--pipeline predictor|wavelet]\n";
        return 1;
    }

    std::string input_file;
    std::string output_file;

    if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        std::cout << "Usage: xtm encode <input.tif> -o <output.xtm> [--precision <value>] [--pipeline predictor|wavelet]\n";
        return 0;
    }
    double precision = 1.0;
    bool disable_quadtree = false;
    coding::ContextModel model = coding::ContextModel::Simple;
    analyzer::PipelineType pipeline_type = analyzer::PipelineType::Predictor;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        bool has_next = (i + 1 < argc);

        if (arg == "-o" && has_next) output_file = argv[++i];
        else if ((arg == "--scale" || arg == "--precision") && has_next) precision = parse_precision(argv[++i]);
        else if (arg == "--disable-quadtree") disable_quadtree = true;
        else if (arg == "--context" && has_next) {
            std::string c = argv[++i];
            if (c != "simple" && c != "extended") { std::cerr << "Unknown context: " << c << "\n"; return 1; }
            model = (c == "extended") ? coding::ContextModel::Extended : coding::ContextModel::Simple;
        } else if (arg == "--pipeline" && has_next) {
            std::string p = argv[++i];
            if (p != "predictor" && p != "wavelet") { std::cerr << "Unknown pipeline: " << p << "\n"; return 1; }
            pipeline_type = (p == "wavelet") ? analyzer::PipelineType::Wavelet : analyzer::PipelineType::Predictor;
        } else if (input_file.empty() && arg[0] != '-') input_file = arg;
        else {
            std::cerr << "Invalid or incomplete argument: " << arg << "\n";
            return 1;
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: Input file is required.\n";
        return 1;
    }

    if (output_file.empty()) {
        std::cerr << "Error: Output file (-o) is required.\n";
        return 1;
    }

    if (precision <= 0.0) {
        std::cerr << "Error: invalid --precision value.\n";
        return 1;
    }

    try {
        std::cout << "Loading dataset: " << input_file << "...\n";

        std::cout << "Encoding terrain (precision=" << precision << ") to " << output_file << "...\n";
        if (disable_quadtree) {
            std::cout << "Quadtree disabled (using fixed 64x64 blocks).\n";
        }

        std::cout << "Pipeline Type: " << (pipeline_type == analyzer::PipelineType::Wavelet ? "Wavelet" : "Predictor") << "\n";

        api::EncodeOptions options(precision, model, pipeline_type, disable_quadtree);
        auto result = api::encode_file(input_file, output_file, options);

        std::cout << "Partitioned into " << result.total_blocks << " independent blocks across superblocks.\n";

        print_pipeline_statistics(result, pipeline_type);

        std::cout << "\n=== Pipeline Profiling ===\n";
        std::cout << "  Quantization:         " << result.time_load_ms << " ms\n";
        std::cout << "  Quadtree/Prediction:  " << result.time_quadtree_ms << " ms\n";
        std::cout << "  Entropy Coding:       " << result.time_entropy_ms << " ms\n";
        std::cout << "  Container IO:         " << result.time_io_ms << " ms\n";
        std::cout << "  Total Execution:      " << result.time_total_ms << " ms\n";
        std::cout << "==========================\n";

        std::cout << "Successfully encoded to " << output_file << "!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli