#include "xtm/transform/Wavelet.hpp"
#include <utility>
#include <algorithm>

namespace xtm::transform {

void CDF53Transform::forward_1d(std::vector<int32_t>& data, uint32_t offset, uint32_t stride, uint32_t length, std::vector<int32_t>& scratch) {
    if (length <= 1) return;
    
    // 1. Predict
    for (uint32_t n = 0; n < length / 2; ++n) {
        uint32_t i_even = 2 * n;
        uint32_t i_odd = 2 * n + 1;
        uint32_t i_next_even = (i_even + 2 < length) ? (i_even + 2) : i_even;
        
        int32_t x_2n = data[offset + i_even * stride];
        int32_t x_2n_plus_2 = data[offset + i_next_even * stride];
        
        data[offset + i_odd * stride] -= (x_2n + x_2n_plus_2) >> 1;
    }
    
    // 2. Update
    for (uint32_t n = 0; n < (length + 1) / 2; ++n) {
        uint32_t i_even = 2 * n;
        uint32_t i_prev_odd = (n > 0) ? (2 * n - 1) : 1;
        uint32_t i_odd = (2 * n + 1 < length) ? (2 * n + 1) : i_prev_odd;
        
        int32_t d_n_minus_1 = data[offset + i_prev_odd * stride];
        int32_t d_n = data[offset + i_odd * stride];
        
        data[offset + i_even * stride] += (d_n_minus_1 + d_n + 2) >> 2;
    }
    
    // 3. Pack (in-place using scratch as a swap buffer)
    std::vector<int32_t>& temp = scratch;
    if (temp.size() < length) temp.resize(length);
    uint32_t half = (length + 1) / 2;
    for (uint32_t i = 0; i < length; ++i) {
        if (i % 2 == 0) {
            temp[i / 2] = data[offset + i * stride];
        } else {
            temp[half + i / 2] = data[offset + i * stride];
        }
    }
    for (uint32_t i = 0; i < length; ++i) {
        data[offset + i * stride] = temp[i];
    }
}

void CDF53Transform::inverse_1d(std::vector<int32_t>& data, uint32_t offset, uint32_t stride, uint32_t length, std::vector<int32_t>& scratch) {
    if (length <= 1) return;
    
    // 1. Unpack
    std::vector<int32_t>& temp = scratch;
    if (temp.size() < length) temp.resize(length);
    for (uint32_t i = 0; i < length; ++i) {
        temp[i] = data[offset + i * stride];
    }
    uint32_t half = (length + 1) / 2;
    for (uint32_t i = 0; i < length; ++i) {
        if (i % 2 == 0) {
            data[offset + i * stride] = temp[i / 2];
        } else {
            data[offset + i * stride] = temp[half + i / 2];
        }
    }
    
    // 2. Inverse Update
    for (uint32_t n = 0; n < (length + 1) / 2; ++n) {
        uint32_t i_even = 2 * n;
        uint32_t i_prev_odd = (n > 0) ? (2 * n - 1) : 1;
        uint32_t i_odd = (2 * n + 1 < length) ? (2 * n + 1) : i_prev_odd;
        
        int32_t d_n_minus_1 = data[offset + i_prev_odd * stride];
        int32_t d_n = data[offset + i_odd * stride];
        
        data[offset + i_even * stride] -= (d_n_minus_1 + d_n + 2) >> 2;
    }
    
    // 3. Inverse Predict
    for (uint32_t n = 0; n < length / 2; ++n) {
        uint32_t i_even = 2 * n;
        uint32_t i_odd = 2 * n + 1;
        uint32_t i_next_even = (i_even + 2 < length) ? (i_even + 2) : i_even;
        
        int32_t x_2n = data[offset + i_even * stride];
        int32_t x_2n_plus_2 = data[offset + i_next_even * stride];
        
        data[offset + i_odd * stride] += (x_2n + x_2n_plus_2) >> 1;
    }
}

void CDF53Transform::forward_2d(std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t levels) {
    std::vector<int32_t> scratch(std::max(width, height));
    uint32_t cur_width = width;
    uint32_t cur_height = height;
    
    for (uint32_t l = 0; l < levels; ++l) {
        if (cur_width <= 1 && cur_height <= 1) break;
        
        // Rows
        for (uint32_t y = 0; y < cur_height; ++y) {
            forward_1d(data, y * width, 1, cur_width, scratch);
        }
        
        // Cols
        for (uint32_t x = 0; x < cur_width; ++x) {
            forward_1d(data, x, width, cur_height, scratch);
        }
        
        cur_width = (cur_width + 1) / 2;
        cur_height = (cur_height + 1) / 2;
    }
}

void CDF53Transform::inverse_2d(std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t levels) {
    std::vector<std::pair<uint32_t, uint32_t>> sizes;
    uint32_t cur_width = width;
    uint32_t cur_height = height;
    for (uint32_t l = 0; l < levels; ++l) {
        sizes.push_back({cur_width, cur_height});
        cur_width = (cur_width + 1) / 2;
        cur_height = (cur_height + 1) / 2;
    }
    
    std::vector<int32_t> scratch(std::max(width, height));
    
    for (int l = levels - 1; l >= 0; --l) {
        cur_width = sizes[l].first;
        cur_height = sizes[l].second;
        if (cur_width <= 1 && cur_height <= 1) continue;
        
        // Cols
        for (uint32_t x = 0; x < cur_width; ++x) {
            inverse_1d(data, x, width, cur_height, scratch);
        }
        
        // Rows
        for (uint32_t y = 0; y < cur_height; ++y) {
            inverse_1d(data, y * width, 1, cur_width, scratch);
        }
    }
}

} // namespace xtm::transform
