#include "DecodeCmd.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/io/GDALWriter.hpp"
#include "xtm/terrain/Quantization.hpp"
#include "xtm/partition/Block.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/predictor/Predictors.hpp"
#include "xtm/coding/Decoder.hpp"
#include <string>
#include <cstring>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <unistd.h>

namespace xtm::cli {

int run_decode(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: xtm decode <input.xtm> -o <output.tif>\n";
        return 1;
    }
    
    std::string input_file = argv[1];
    std::string output_file = "";
    int rx = 0, ry = 0, rw = 0, rh = 0;
    bool has_roi = false;
    
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--region" && i + 4 < argc) {
            rx = std::stoi(argv[++i]);
            ry = std::stoi(argv[++i]);
            rw = std::stoi(argv[++i]);
            rh = std::stoi(argv[++i]);
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
        
        container::XtmReader reader(input_file);
        const auto& header = reader.get_header();
        
        if (!has_roi) {
            rw = header.grid_width;
            rh = header.grid_height;
        }
        
        std::cout << "Target ROI: x=" << rx << ", y=" << ry << ", w=" << rw << ", h=" << rh << "\n";
        
        terrain::IntGrid roi_grid;
        roi_grid.width = rw;
        roi_grid.height = rh;
        roi_grid.data.resize(rw * rh, 0);
        roi_grid.nodata_mask.resize(rw * rh, false);
        
        std::cout << "Detected Pipeline: " << (header.pipeline_id == container::XtmHeader::PIPELINE_WAVELET ? "Wavelet" : "Predictor") << "\n";
        auto dec_result = coding::XtmDecoder::decode(reader, roi_grid, rx, ry, rw, rh, 0);
        uint32_t blocks_decoded = dec_result.blocks_decoded;

        
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end_time - start_time;
        std::cout << "Decoded " << blocks_decoded << " blocks in " << diff.count() << " seconds.\n";
        
        std::cout << "Dequantizing and writing to " << output_file << "...\n";
        
        GeoTransform gt = header.transform;
        if (has_roi) {
            gt.origin_x += rx * gt.pixel_width + ry * gt.rotation_x;
            gt.origin_y += rx * gt.rotation_y + ry * gt.pixel_height;
        }
        
        std::optional<double> nodata_val = std::nullopt;
        if (header.flags & container::XtmHeader::FLAG_HAS_NODATA) {
            nodata_val = header.nodata_value;
        }
        TerrainBuffer out_buffer = terrain::dequantize(roi_grid, header.scale, nodata_val, gt);
        out_buffer.wkt_projection = header.wkt_projection;
        io::write_gdal(output_file, out_buffer.view());
        
        std::cout << "Successfully exported TIFF!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}

} // namespace xtm::cli
