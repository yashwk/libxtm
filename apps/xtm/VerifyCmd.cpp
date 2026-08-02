#include "VerifyCmd.hpp"
#include "DecodeCmd.hpp"
#include "xtm/io/GDALReader.hpp"
#include "xtm/Terrain.hpp"
#include "xtm/container/IO.hpp"
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
    double scale = reader.get_header().res_x;
    
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

    std::string temp_tif = "/tmp/xtm_verify_temp.tif";

    const char* decode_args[] = {
        "decode",
        xtm_file.c_str(),
        "-o",
        temp_tif.c_str()
    };

    std::cout << "Decoding " << xtm_file << " to temporary file...\n";
    int decode_res = run_decode(4, const_cast<char**>(decode_args));
    if (decode_res != 0) {
        std::cerr << "Verification failed: Decoder returned error code " << decode_res << "\n";
        return 1;
    }

    try {
        std::cout << "Reading original " << tif_file << "...\n";
        auto orig = io::read_gdal(tif_file);
        
        std::cout << "Reading decoded " << temp_tif << "...\n";
        auto dec = io::read_gdal(temp_tif);

        if (orig.width() != dec.width() || orig.height() != dec.height()) {
            std::cerr << "Verification failed: Dimension mismatch (" 
                      << orig.width() << "x" << orig.height() << " vs "
                      << dec.width() << "x" << dec.height() << ")\n";
            std::filesystem::remove(temp_tif);
            return 1;
        }

        size_t mismatches = 0;
        size_t total = orig.width() * orig.height();
        
        for (size_t i = 0; i < total; ++i) {
            float o = orig.data()[i];
            float d = dec.data()[i];
            
            bool o_is_nodata = orig.nodata_value.has_value() && o == orig.nodata_value.value();
            bool d_is_nodata = dec.nodata_value.has_value() && d == dec.nodata_value.value();
            
            if (o_is_nodata != d_is_nodata) {
                mismatches++;
            } else if (!o_is_nodata && std::abs(o - d) > (0.5f * scale) + 1e-4f) {
                mismatches++;
            }
        }

        std::filesystem::remove(temp_tif);

        if (mismatches > 0) {
            std::cerr << "Verification failed: " << mismatches << " of " << total << " pixels differ.\n";
            return 1;
        }

        std::cout << "Verification passed: " << total << " pixels are identical.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Verification failed: Error during comparison - " << e.what() << "\n";
        std::filesystem::remove(temp_tif);
        return 1;
    }

    return 0;
}

} // namespace xtm::cli
