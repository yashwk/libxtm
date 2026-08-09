#pragma once
#include "Types.hpp"
#include <vector>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>

namespace xtm {

struct TerrainView {
    const double* data;
    std::uint32_t width;
    std::uint32_t height;
    
    GeoTransform transform;
    std::optional<double> nodata_value;
    std::string wkt_projection;

    double get(std::uint32_t x, std::uint32_t y) const {
        return data[y * width + x];
    }
};

class TerrainBuffer {
public:
    TerrainBuffer(std::uint32_t w, std::uint32_t h) 
        : width_(w), height_(h), data_(w * h) {}

    double* data() { return data_.data(); }
    const double* data() const { return data_.data(); }

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }

    GeoTransform transform;
    std::optional<double> nodata_value;
    std::string wkt_projection;

    TerrainView view() const {
        return TerrainView{
            data_.data(),
            width_,
            height_,
            transform,
            nodata_value,
            wkt_projection
        };
    }

private:
    std::uint32_t width_;
    std::uint32_t height_;
    std::vector<double> data_;
};

} // namespace xtm
