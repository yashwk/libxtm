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

IntGrid quantize(const TerrainView& view, double scale = 1.0);
TerrainBuffer dequantize(const IntGrid& grid, double scale = 1.0, std::optional<double> nodata_value = std::nullopt, GeoTransform transform = {});

} // namespace xtm::terrain
