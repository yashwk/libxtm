#include "xtm/terrain/Quantization.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace xtm::terrain {

IntGrid quantize_pixels(const TerrainView& view, double precision) {
    IntGrid grid;
    grid.width = view.width;
    grid.height = view.height;
    grid.data.resize(static_cast<std::size_t>(grid.width) * grid.height);
    grid.nodata_mask.resize(static_cast<std::size_t>(grid.width) * grid.height, 0);

    double inv_scale = 1.0 / precision;

    for (std::uint32_t y = 0; y < view.height; ++y) {
        for (std::uint32_t x = 0; x < view.width; ++x) {
            double val = view.get(x, y);
            uint32_t idx = y * view.width + x;
            if (view.nodata_value && val == *view.nodata_value) {
                grid.nodata_mask[idx] = 1;
                grid.data[idx] = 0; // Temporary placeholder
            } else {
                double q = std::round(val * inv_scale);
                if (!std::isfinite(q)) {
                    // NaN/Inf values: deterministic placeholder instead of UB
                    q = 0.0;
                } else if (q >= 2147483647.0) {
                    q = static_cast<double>(std::numeric_limits<int32_t>::max());
                } else if (q <= -2147483648.0) {
                    q = static_cast<double>(std::numeric_limits<int32_t>::lowest());
                }
                grid.data[idx] = static_cast<int32_t>(q);
            }
        }
    }

    return grid;
}

void inpaint(IntGrid& grid) {
    const std::uint32_t w = grid.width;
    const std::uint32_t h = grid.height;
    const std::size_t n = static_cast<std::size_t>(w) * h;

    // State per cell: 0 = unfilled & not queued, 1 = queued, 2 = filled
    std::vector<std::uint8_t> state(n, 0);
    std::size_t any = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!grid.nodata_mask[i]) {
            state[i] = 2;
            any++;
        }
    }
    if (any == 0 || any == n) return;

    std::vector<std::uint32_t> frontier;
    std::vector<std::uint32_t> next_frontier;
    frontier.reserve(std::min<std::size_t>(n, 2 * (w + h)));
    next_frontier.reserve(frontier.capacity());

    auto seed = [&](std::uint32_t idx) {
        std::uint32_t x = idx % w;
        std::uint32_t y = idx / w;
        if ((x > 0 && state[idx - 1] == 2) ||
            (x + 1 < w && state[idx + 1] == 2) ||
            (y > 0 && state[idx - w] == 2) ||
            (y + 1 < h && state[idx + w] == 2)) {
            state[idx] = 1;
            frontier.push_back(idx);
        }
    };
    for (std::uint32_t idx = 0; idx < n; ++idx) {
        if (state[idx] == 0) seed(idx);
    }

    while (!frontier.empty()) {
        // Phase 1: compute values from filled (ring < current) neighbors
        for (std::uint32_t idx : frontier) {
            std::uint32_t x = idx % w;
            std::uint32_t y = idx / w;
            int64_t sum = 0;
            int count = 0;
            if (x > 0 && state[idx - 1] == 2) { sum += grid.data[idx - 1]; count++; }
            if (x + 1 < w && state[idx + 1] == 2) { sum += grid.data[idx + 1]; count++; }
            if (y > 0 && state[idx - w] == 2) { sum += grid.data[idx - w]; count++; }
            if (y + 1 < h && state[idx + w] == 2) { sum += grid.data[idx + w]; count++; }
            if (count > 0) {
                grid.data[idx] = static_cast<int32_t>(sum / count);
            }
        }
        // Phase 2: mark filled, queue newly adjacent unfilled cells
        next_frontier.clear();
        for (std::uint32_t idx : frontier) {
            state[idx] = 2;
            std::uint32_t x = idx % w;
            std::uint32_t y = idx / w;
            if (x > 0 && state[idx - 1] == 0) { state[idx - 1] = 1; next_frontier.push_back(idx - 1); }
            if (x + 1 < w && state[idx + 1] == 0) { state[idx + 1] = 1; next_frontier.push_back(idx + 1); }
            if (y > 0 && state[idx - w] == 0) { state[idx - w] = 1; next_frontier.push_back(idx - w); }
            if (y + 1 < h && state[idx + w] == 0) { state[idx + w] = 1; next_frontier.push_back(idx + w); }
        }
        frontier.swap(next_frontier);
    }
}

IntGrid quantize(const TerrainView& view, double precision) {
    IntGrid grid = quantize_pixels(view, precision);
    if (view.nodata_value) {
        inpaint(grid);
    }
    return grid;
}

void dequantize_rows(const IntGrid& grid, std::uint32_t y0, std::uint32_t nrows,
                     double precision, std::optional<double> nodata_value, double* out) {
    const std::uint32_t w = grid.width;
    for (std::uint32_t y = 0; y < nrows; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            std::size_t idx = static_cast<std::size_t>(y0 + y) * w + x;
            double& dst = out[static_cast<std::size_t>(y) * w + x];
            if (!grid.nodata_mask.empty() && grid.nodata_mask[idx] && nodata_value.has_value()) {
                dst = *nodata_value;
            } else {
                dst = static_cast<double>(grid.data[idx] * precision);
            }
        }
    }
}

TerrainBuffer dequantize(const IntGrid& grid, double precision, std::optional<double> nodata_value, GeoTransform transform) {
    TerrainBuffer buffer(grid.width, grid.height);
    buffer.nodata_value = nodata_value;
    buffer.transform = transform;
    dequantize_rows(grid, 0, grid.height, precision, nodata_value, buffer.data());
    return buffer;
}

} // namespace xtm::terrain
