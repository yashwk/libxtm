#pragma once
#include <cstdint>

namespace xtm {

struct GeoTransform {
    double origin_x = 0.0;
    double pixel_width = 1.0;
    double rotation_x = 0.0;
    double origin_y = 0.0;
    double rotation_y = 0.0;
    double pixel_height = 1.0;
};

} // namespace xtm
