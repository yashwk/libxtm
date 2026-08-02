#pragma once
#include <cstdint>

namespace xtm {

enum class SampleType {
    Float32,
    Int16,
    Int32
};

struct BoundingBox {
    double min_x;
    double min_y;
    double max_x;
    double max_y;
};

struct GeoTransform {
    double origin_x;
    double pixel_width;
    double rotation_x;
    double origin_y;
    double rotation_y;
    double pixel_height;
};

} // namespace xtm
