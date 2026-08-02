#pragma once
#include <vector>
#include <cstdint>
#include "xtm/Terrain.hpp"

namespace xtm::terrain {

struct IntGrid {
    std::vector<int32_t> data;
    std::vector<bool> nodata_mask; // true if pixel is NoData
    std::uint32_t width;
    std::uint32_t height;

    int32_t get(std::uint32_t x, std::uint32_t y) const {
        return data[y * width + x];
    }
};

IntGrid quantize(const TerrainView& view, double scale = 1.0);
TerrainBuffer dequantize(const IntGrid& grid, double scale = 1.0, std::optional<float> nodata_value = std::nullopt, GeoTransform transform = {});

} // namespace xtm::terrain
