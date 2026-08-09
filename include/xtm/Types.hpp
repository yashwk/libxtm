#pragma once
#include <cstdint>

namespace xtm {

struct GeoTransform {
    double origin_x;
    double pixel_width;
    double rotation_x;
    double origin_y;
    double rotation_y;
    double pixel_height;
};

} // namespace xtm
