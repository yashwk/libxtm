#pragma once
#include <cstdint>
#include <vector>
#include <iostream>

namespace xtm::container {

struct XtmHeader {
    char magic[4] = {'X', 'T', 'M', '\0'};
    uint16_t version = 3; // v2 adds context_model, v3 adds block CRC32
    uint16_t flags = 0;
    
    static constexpr uint16_t FLAG_USE_WAVELET = 1 << 0;
    static constexpr uint16_t FLAG_HAS_NODATA = 1 << 1;
    static constexpr uint16_t FLAG_WAVELET_FIRST = 1 << 2;

    // Context model used by the arithmetic coder at encode time so the decoder
    // can reproduce the exact same context set. 0 = Simple, 1 = Extended.
    // Decoding with a mismatched model silently produces garbage.
    uint16_t context_model = 0;
    
    float nodata_value = 0.0f;
    
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;
    uint32_t epsg_crs = 0;
    
    uint32_t grid_width = 0;
    uint32_t grid_height = 0;
    double res_x = 0.0;
    double res_y = 0.0;
    
    uint64_t index_offset = 0;
    
    void write(std::ostream& os) const;
    void read(std::istream& is);
};

struct BlockIndexEntry {
    uint32_t block_x = 0;
    uint32_t block_y = 0;
    uint32_t block_width = 0;
    uint32_t block_height = 0;
    
    uint64_t byte_offset = 0;
    uint64_t byte_length = 0;
    
    uint32_t checksum = 0; // CRC32 of the bitstream
    
    void write(std::ostream& os) const;
    void read(std::istream& is, uint16_t version);
};

} // namespace xtm::container
