#pragma once
#include "xtm/Terrain.hpp"
#include "xtm/terrain/Quantization.hpp"
#include <string>

namespace xtm::io {

struct RasterInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    GeoTransform transform;
    std::optional<double> nodata_value;
    std::string wkt_projection;

    // Raw elevation statistics accumulated during the windowed read
    // (nodata cells are excluded).
    double raw_min = 0.0;
    double raw_max = 0.0;
    double raw_mean = 0.0;
    double raw_stddev = 0.0;
    std::size_t raw_valid_pixels = 0;
};

TerrainBuffer read_gdal(const std::string& path);

// Reads and quantizes the raster in row-band windows directly into an IntGrid,
// avoiding the full Float64 in-memory buffer. Peaks at O(width * band_rows).
// Inpaints nodata after the full grid is materialized.
terrain::IntGrid read_gdal_quantized(const std::string& path, double precision, RasterInfo& info);

}
