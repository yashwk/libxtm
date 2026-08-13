#pragma once
#include "xtm/Terrain.hpp"
#include "xtm/terrain/Quantization.hpp"
#include <array>
#include <string>
#include <vector>

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

    // p1/p25/p50/p75/p99 of the raw values, estimated from a deterministic
    // stride sample of the windowed read.
    std::array<double, 5> raw_percentiles = {0.0, 0.0, 0.0, 0.0, 0.0};
    // Coarse elevation distribution: 50 buckets over [raw_min, raw_max],
    // normalized to [0, 1] by the peak bucket. Empty when no valid pixels.
    std::vector<double> elevation_histogram;

    // Georeferencing from the dataset header (empty when absent).
    std::string crs;         // e.g. "EPSG:32633" (authority code when known)
    std::string pixel_units; // e.g. "m", "deg"
    bool has_georeference = false;
    double pixel_width = 0.0; // CRS units per pixel
    double pixel_height = 0.0;
    double bbox_min_x = 0.0;
    double bbox_min_y = 0.0;
    double bbox_max_x = 0.0;
    double bbox_max_y = 0.0;
};

TerrainBuffer read_gdal(const std::string& path);

// Reads and quantizes the raster in row-band windows directly into an IntGrid,
// avoiding the full Float64 in-memory buffer. Peaks at O(width * band_rows).
// Inpaints nodata after the full grid is materialized.
terrain::IntGrid read_gdal_quantized(const std::string& path, double precision, RasterInfo& info);

}
