#include "DecodeCmd.hpp"
#include "xtm/Api.hpp"
#include "xtm/container/Header.hpp"
#include <iostream>
#include <string>
#include <chrono>

namespace xtm::cli {

int run_decode(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: xtm decode <input.xtm> -o <output.tif>\n";
        return 1;
    }

    std::string input_file = argv[1];
    std::string output_file = "";
    long rx = 0, ry = 0, rw = 0, rh = 0;
    bool has_roi = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--region" && i + 4 < argc) {
            rx = std::stol(argv[++i]);
            ry = std::stol(argv[++i]);
            rw = std::stol(argv[++i]);
            rh = std::stol(argv[++i]);
            has_roi = true;
        }
    }

    if (output_file.empty()) {
        std::cerr << "Error: Output file (-o) is required.\n";
        return 1;
    }

    if (has_roi && (rx < 0 || ry < 0 || rw <= 0 || rh <= 0)) {
        std::cerr << "Error: --region requires x >= 0, y >= 0, width > 0, height > 0.\n";
        return 1;
    }

    try {
        std::cout << "Opening XTM container: " << input_file << "...\n";
        auto start_time = std::chrono::high_resolution_clock::now();

        auto info = api::info_file(input_file);
        const auto& header = info.header;

        api::DecodeOptions options;
        if (has_roi) {
            options.region_x = static_cast<uint32_t>(rx);
            options.region_y = static_cast<uint32_t>(ry);
            options.region_width = static_cast<uint32_t>(rw);
            options.region_height = static_cast<uint32_t>(rh);
        }

        if (!has_roi) {
            rw = header.grid_width;
            rh = header.grid_height;
        }

        std::cout << "Target ROI: x=" << rx << ", y=" << ry << ", w=" << rw << ", h=" << rh << "\n";

        std::cout << "Detected Pipeline: " << (header.pipeline_id == container::XtmHeader::PIPELINE_WAVELET ? "Wavelet" : "Predictor") << "\n";
        auto dec_result = api::decode_file(input_file, output_file, options);

        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end_time - start_time;
        std::cout << "Decoded " << dec_result.blocks_decoded << " blocks in " << diff.count() << " seconds.\n";

        std::cout << "Successfully exported TIFF!\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli