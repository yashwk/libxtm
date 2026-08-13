#include "InfoCmd.hpp"
#include "xtm/Api.hpp"
#include <iostream>
#include <iomanip>

namespace xtm::cli {

int run_info(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: xtm info <input.xtm>\n";
        return 1;
    }

    std::string input_path = argv[1];
    if (input_path == "-h" || input_path == "--help") {
        std::cout << "Usage: xtm info <input.xtm>\n";
        return 0;
    }

    try {
        auto info = api::info_file(input_path);
        const auto& header = info.header;

        std::cout << "--- XTM File Info ---\n";
        std::cout << "Magic: " << header.magic[0] << header.magic[1] << header.magic[2] << "\n";
        std::cout << "Flags: " << header.flags << "\n";
        std::cout << "Context Model: " << header.context_model << "\n";
        std::cout << "NoData Value: " << header.nodata_value << "\n";
        if (!header.wkt_projection.empty()) {
            std::cout << "Projection: " << header.wkt_projection.substr(0, 100) << (header.wkt_projection.size() > 100 ? "..." : "") << "\n";
        }
        std::cout << "Origin: [" << header.transform.origin_x << ", " << header.transform.origin_y << "]\n";
        std::cout << "Grid: " << header.grid_width << "x" << header.grid_height << "\n";
        std::cout << "Pixel Size: " << header.transform.pixel_width << " x " << header.transform.pixel_height << "\n";
        std::cout << "Quantization Precision: " << header.precision << "\n";
        std::cout << "Index Offset: " << header.index_offset << "\n";

        std::cout << "\n--- Block Index (" << info.block_count << " blocks) ---\n";

        for (const auto& entry : info.index) {
            std::cout << "Block [" << entry.block_x << "," << entry.block_y << "] "
                      << "Size [" << entry.block_width << "x" << entry.block_height << "] "
                      << "Offset: " << entry.byte_offset << " "
                      << "Length: " << entry.byte_length << " bytes\n";
        }

        std::cout << "\nTotal Block Data: " << info.total_payload_bytes << " bytes\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli