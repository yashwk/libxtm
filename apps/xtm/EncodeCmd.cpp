#include "EncodeCmd.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/terrain/Quantization.hpp"
#include "xtm/analyzer/Selector.hpp"
#include "xtm/analyzer/Statistics.hpp"
#include "xtm/partition/Quadtree.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/predictor/Predictors.hpp"
#include "xtm/coding/Encoder.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <map>
#include <chrono>
#include <iomanip>
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <memory>
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

void print_pipeline_statistics(const coding::EncodeResult& result, analyzer::PipelineType pipeline_type, const std::vector<const predictor::Predictor*>& predictors_list) {
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

    struct Stat {
        uint32_t id;
        std::string name;
        uint32_t count;
    };
    
    std::vector<Stat> stats;
    for (const auto& kv : result.predictor_stats) {
        if (kv.second.count == 0) continue;
        std::string name = "Unknown";
        if (kv.first < predictors_list.size()) {
            name = predictors_list[kv.first]->name();
        }
        stats.push_back({
            kv.first, name, kv.second.count
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
    
    using namespace std::chrono;
    
    try {
        auto t_start = high_resolution_clock::now();
        
        std::cout << "Loading dataset: " << input_file << "...\n";
        
        std::cout << "Encoding terrain (precision=" << precision << ") to " << output_file << "...\n";
        if (disable_quadtree) {
            std::cout << "Quadtree disabled (using fixed 64x64 blocks).\n";
        }
        
        auto t_quant_start = high_resolution_clock::now();
        io::RasterInfo rinfo;
        auto cgrid = io::read_gdal_quantized(input_file, precision, rinfo);
        auto t_quant_end = high_resolution_clock::now();
        double time_quant = duration<double, std::milli>(t_quant_end - t_quant_start).count();
        
        predictor::PredictorBank bank;
        std::vector<const predictor::Predictor*> predictors_list = bank.ordered();
        
        container::XtmHeader header;
        header.grid_width = cgrid.width;
        header.grid_height = cgrid.height;
        header.precision = precision;
        header.wkt_projection = rinfo.wkt_projection;
        header.context_model = (model == coding::ContextModel::Extended) ? 1 : 0;
        if (rinfo.nodata_value.has_value()) {
            header.flags |= container::XtmHeader::FLAG_HAS_NODATA;
            header.nodata_value = *rinfo.nodata_value;
        }
        header.pipeline_id = (pipeline_type == analyzer::PipelineType::Wavelet) ? container::XtmHeader::PIPELINE_WAVELET : container::XtmHeader::PIPELINE_PREDICTOR;
        if (disable_quadtree) {
            header.flags |= container::XtmHeader::FLAG_DISABLE_QUADTREE;
        }
        
        std::cout << "Pipeline Type: " << (pipeline_type == analyzer::PipelineType::Wavelet ? "Wavelet" : "Predictor") << "\n";
        
        header.transform = rinfo.transform;
        
        // Output file creation
        container::XtmWriter writer(output_file, header);
        
        coding::PipelineContext ctx(precision, model, pipeline_type);
        ctx.disable_quadtree = disable_quadtree;
        
        auto encode_result = coding::XtmEncoder::encode(cgrid, writer, ctx);
        
        std::cout << "Partitioned into " << encode_result.total_blocks << " independent blocks across superblocks.\n";
        
        print_pipeline_statistics(encode_result, pipeline_type, predictors_list);
        
        writer.finalize();
        
        auto t_end = high_resolution_clock::now();
        double time_total = duration<double, std::milli>(t_end - t_start).count();
        
        std::cout << "\n=== Pipeline Profiling ===\n";
        std::cout << "  Quantization:         " << time_quant << " ms\n";
        std::cout << "  Quadtree/Prediction:  " << encode_result.time_quadtree << " ms\n";
        std::cout << "  Entropy Coding:       " << encode_result.time_entropy << " ms\n";
        std::cout << "  Container IO:         " << encode_result.time_io << " ms\n";
        std::cout << "  Total Execution:      " << time_total << " ms\n";
        std::cout << "==========================\n";
        
        std::cout << "Successfully encoded to " << output_file << "!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

} // namespace xtm::cli
