#include "InfoCmd.hpp"
#include "xtm/container/IO.hpp"
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
        container::XtmReader reader(input_path);
        const auto& header = reader.get_header();
        
        std::cout << "--- XTM File Info ---\n";
        std::cout << "Magic: " << header.magic[0] << header.magic[1] << header.magic[2] << "\n";
        std::cout << "Version: " << header.version << "\n";
        std::cout << "Flags: " << header.flags << "\n";
        std::cout << "Context Model: " << header.context_model << "\n";
        std::cout << "NoData Value: " << header.nodata_value << "\n";
        std::cout << "EPSG CRS: " << header.epsg_crs << "\n";
        std::cout << "Extent: [" << header.min_x << ", " << header.min_y << "] to [" << header.max_x << ", " << header.max_y << "]\n";
        std::cout << "Grid: " << header.grid_width << "x" << header.grid_height << "\n";
        std::cout << "Resolution: " << header.res_x << " x " << header.res_y << "\n";
        std::cout << "Index Offset: " << header.index_offset << "\n";
        
        const auto& index = reader.get_index();
        std::cout << "\n--- Block Index (" << index.size() << " blocks) ---\n";
        
        uint64_t total_compressed = 0;
        for (const auto& entry : index) {
            std::cout << "Block [" << entry.block_x << "," << entry.block_y << "] "
                      << "Size [" << entry.block_width << "x" << entry.block_height << "] "
                      << "Offset: " << entry.byte_offset << " "
                      << "Length: " << entry.byte_length << " bytes\n";
            total_compressed += entry.byte_length;
        }
        
        std::cout << "\nTotal Block Data: " << total_compressed << " bytes\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli
