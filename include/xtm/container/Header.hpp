#pragma once
#include <cstdint>
#include <vector>
#include <iostream>
#include <string>
#include "xtm/Terrain.hpp"

namespace xtm::container {

struct XtmHeader {
    char magic[4] = {'X', 'T', 'M', '\0'};
    uint16_t version = 4; // v4 adds WKT projection string and full 6-parameter GeoTransform
    uint16_t flags = 0;
    
    static constexpr uint16_t FLAG_HAS_NODATA = 1 << 1;
    static constexpr uint16_t FLAG_DISABLE_QUADTREE = 1 << 3;

    static constexpr uint8_t PIPELINE_PREDICTOR = 0;
    static constexpr uint8_t PIPELINE_WAVELET = 1;

    uint8_t pipeline_id = PIPELINE_PREDICTOR;
    uint16_t context_model = 0;
    
    double nodata_value = 0.0;
    
    GeoTransform transform;
    std::string wkt_projection;
    
    uint32_t grid_width = 0;
    uint32_t grid_height = 0;
    
    double precision = 1.0;
    
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
