#include "VerifyCmd.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/Terrain.hpp"
#include "xtm/container/IO.hpp"
#include "xtm/coding/Decoder.hpp"
#include "xtm/terrain/Quantization.hpp"
#include <iostream>
#include <string>
#include <cmath>
#include <filesystem>
#include <memory>

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

    std::unique_ptr<container::XtmReader> reader_ptr;
    try {
        reader_ptr = std::make_unique<container::XtmReader>(xtm_file);
    } catch (const std::exception& e) {
        std::cerr << "Verification failed: " << e.what() << "\n";
        return 1;
    }
    
    container::XtmReader& reader = *reader_ptr;

    if (argc == 2) {
        std::cout << "Verifying block checksums for " << xtm_file << "...\n";
        const auto& index = reader.get_index();
        for (const auto& entry : index) {
            try {
                reader.read_block(entry);
            } catch (const std::exception& e) {
                std::cerr << "Verification failed: Block [" << entry.block_x << "," << entry.block_y << "] corrupt - " << e.what() << "\n";
                return 1;
            }
        }
        std::cout << "Verification passed: All " << index.size() << " block checksums are valid.\n";
        return 0;
    }

    std::string tif_file = argv[2];

    try {
        std::cout << "Decoding " << xtm_file << " into memory...\n";
        
        const auto& header = reader.get_header();
        int rw = header.grid_width;
        int rh = header.grid_height;
        
        terrain::IntGrid roi_grid;
        roi_grid.width = rw;
        roi_grid.height = rh;
        roi_grid.data.resize(rw * rh, 0);
        roi_grid.nodata_mask.resize(rw * rh, false);
        
        std::cout << "Detected Pipeline: " << (header.pipeline_id == container::XtmHeader::PIPELINE_WAVELET ? "Wavelet" : "Predictor") << "\n";
        
        reader.get_index();
        coding::XtmDecoder::decode(reader, roi_grid, 0, 0, rw, rh, 0);
        
        std::cout << "Reading and quantizing original " << tif_file << "...\n";
        io::RasterInfo orig_info;
        auto orig_grid = io::read_gdal_quantized(tif_file, header.precision, orig_info);

        if (orig_info.width != roi_grid.width || orig_info.height != roi_grid.height) {
            std::cerr << "Verification failed: Dimension mismatch (" 
                      << orig_info.width << "x" << orig_info.height << " vs "
                      << roi_grid.width << "x" << roi_grid.height << ")\n";
            return 1;
        }

        size_t mismatches = 0;
        size_t total = static_cast<size_t>(orig_info.width) * orig_info.height;
        
        for (size_t i = 0; i < total; ++i) {
            bool o_nodata = orig_grid.nodata_mask[i];
            bool d_nodata = roi_grid.nodata_mask[i];
            
            if (o_nodata != d_nodata) {
                mismatches++;
            } else if (!o_nodata && orig_grid.data[i] != roi_grid.data[i]) {
                mismatches++;
            }
        }

        if (mismatches > 0) {
            std::cerr << "Verification failed: " << mismatches << " of " << total << " pixels differ.\n";
            return 1;
        }

        std::cout << "Verification passed: " << total << " pixels are identical.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Verification failed: Error during comparison - " << e.what() << "\n";
        return 1;
    }

    return 0;
}

} // namespace xtm::cli
