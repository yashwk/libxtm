#pragma once
#include <vector>
#include <cstdint>
#include "xtm/Terrain.hpp"

namespace xtm::terrain {

struct IntGrid {
    std::vector<int32_t> data;
    std::vector<uint8_t> nodata_mask; // true (1) if pixel is NoData
    std::uint32_t width;
    std::uint32_t height;

    int32_t get(std::uint32_t x, std::uint32_t y) const {
        return data[y * width + x];
    }
};

// Elementwise quantization without inpainting; nodata cells get value 0 + mask=1.
IntGrid quantize_pixels(const TerrainView& view, double precision = 1.0);

// Fill nodata cells from filled neighbors (iterative ring inpainting; data
// values are filled but nodata_mask entries stay set).
void inpaint(IntGrid& grid);

IntGrid quantize(const TerrainView& view, double precision = 1.0);
TerrainBuffer dequantize(const IntGrid& grid, double precision = 1.0, std::optional<double> nodata_value = std::nullopt, GeoTransform transform = {});

// Dequantize rows [y0, y0+nrows) into a caller-owned row-major buffer of nrows*width doubles.
void dequantize_rows(const IntGrid& grid, std::uint32_t y0, std::uint32_t nrows,
                     double precision, std::optional<double> nodata_value, double* out);

} // namespace xtm::terrain
