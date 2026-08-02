#pragma once
#include <vector>
#include <cstdint>

namespace xtm::transform {

class CDF53Transform {
public:
    // Transforms the block of residuals in-place using 2D CDF 5/3 lifting.
    // levels specifies how many decomposition levels to apply.
    // width and height are the full dimensions of the data buffer.
    static void forward_2d(std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t levels);
    
    // Inverses the 2D CDF 5/3 lifting transform.
    static void inverse_2d(std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t levels);

private:
    static void forward_1d(std::vector<int32_t>& data, uint32_t offset, uint32_t stride, uint32_t length, std::vector<int32_t>& scratch);
    static void inverse_1d(std::vector<int32_t>& data, uint32_t offset, uint32_t stride, uint32_t length, std::vector<int32_t>& scratch);
};

} // namespace xtm::transform
