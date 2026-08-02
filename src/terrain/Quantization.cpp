#include "xtm/terrain/Quantization.hpp"
#include <cmath>

namespace xtm::terrain {

IntGrid quantize(const TerrainView& view, double scale) {
    IntGrid grid;
    grid.width = view.width;
    grid.height = view.height;
    grid.data.resize(grid.width * grid.height);
    grid.nodata_mask.resize(grid.width * grid.height, false);

    double inv_scale = 1.0 / scale;
    bool has_nodata = false;

    for (std::uint32_t y = 0; y < view.height; ++y) {
        for (std::uint32_t x = 0; x < view.width; ++x) {
            float val = view.get(x, y);
            uint32_t idx = y * view.width + x;
            if (view.nodata_value && val == *view.nodata_value) {
                grid.nodata_mask[idx] = true;
                grid.data[idx] = 0; // Temporary placeholder
                has_nodata = true;
            } else {
                grid.data[idx] = static_cast<int32_t>(std::round(val * inv_scale));
            }
        }
    }

    if (has_nodata) {
        // Simple iterative inpainting to eliminate cliffs
        std::vector<bool> filled(grid.width * grid.height, false);
        for (std::size_t i = 0; i < filled.size(); ++i) {
            filled[i] = !grid.nodata_mask[i];
        }

        bool changed = true;
        while (changed) {
            changed = false;
            std::vector<int32_t> next_data = grid.data;
            std::vector<bool> next_filled = filled;

            for (std::uint32_t y = 0; y < view.height; ++y) {
                for (std::uint32_t x = 0; x < view.width; ++x) {
                    uint32_t idx = y * view.width + x;
                    if (!filled[idx]) {
                        int64_t sum = 0;
                        int count = 0;
                        // check neighbors (4-way)
                        if (x > 0 && filled[idx - 1]) { sum += grid.data[idx - 1]; count++; }
                        if (x + 1 < view.width && filled[idx + 1]) { sum += grid.data[idx + 1]; count++; }
                        if (y > 0 && filled[idx - view.width]) { sum += grid.data[idx - view.width]; count++; }
                        if (y + 1 < view.height && filled[idx + view.width]) { sum += grid.data[idx + view.width]; count++; }
                        
                        if (count > 0) {
                            next_data[idx] = static_cast<int32_t>(sum / count);
                            next_filled[idx] = true;
                            changed = true;
                        }
                    }
                }
            }
            grid.data = std::move(next_data);
            filled = std::move(next_filled);
        }
    }

    return grid;
}

TerrainBuffer dequantize(const IntGrid& grid, double scale, std::optional<float> nodata_value, GeoTransform transform) {
    TerrainBuffer buffer(grid.width, grid.height);
    buffer.nodata_value = nodata_value;
    buffer.transform = transform;

    for (std::size_t i = 0; i < grid.data.size(); ++i) {
        if (!grid.nodata_mask.empty() && grid.nodata_mask[i] && nodata_value.has_value()) {
            buffer.data()[i] = *nodata_value;
        } else {
            buffer.data()[i] = static_cast<float>(grid.data[i] * scale);
        }
    }
    return buffer;
}

} // namespace xtm::terrain
