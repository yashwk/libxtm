#include "VerifyCmd.hpp"
#include "xtm/Api.hpp"
#include "xtm/container/Header.hpp"
#include <iostream>
#include <string>

namespace xtm::cli {

int run_verify(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: xtm verify <input.xtm> [input.tif]\n";
        return 1;
    }

    std::string xtm_file = argv[1];

    if (xtm_file == "-h" || xtm_file == "--help") {
        std::cout << "Usage: xtm verify <input.xtm> [input.tif]\n";
        return 0;
    }

    std::string tif_file = (argc >= 3) ? argv[2] : "";

    try {
        if (tif_file.empty()) {
            std::cout << "Verifying block checksums for " << xtm_file << "...\n";
        } else {
            std::cout << "Decoding " << xtm_file << " into memory...\n";
            auto info = api::info_file(xtm_file);
            const auto& header = info.header;
            std::cout << "Detected Pipeline: " << (header.pipeline_id == container::XtmHeader::PIPELINE_WAVELET ? "Wavelet" : "Predictor") << "\n";
            std::cout << "Reading and quantizing original " << tif_file << "...\n";
        }

        auto result = api::verify_file(xtm_file, tif_file);

        if (!result.passed) {
            std::cerr << "Verification failed: " << result.message << "\n";
            return 1;
        }

        if (tif_file.empty()) {
            std::cout << "Verification passed: " << result.message << "\n";
        } else {
            std::cout << result.message << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Verification failed: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli