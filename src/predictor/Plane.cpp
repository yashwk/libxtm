#include "xtm/predictor/Predictors.hpp"
#include <cmath>

namespace xtm::predictor {

PredictionResult PlanePredictor::encode(const partition::BlockView& block) const {
    PredictionResult result;
    result.residuals.reserve(block.width * block.height);
    
    double sum_x = 0, sum_y = 0, sum_z = 0;
    double sum_xx = 0, sum_yy = 0, sum_xy = 0;
    double sum_xz = 0, sum_yz = 0;
    
    int n = block.width * block.height;
    
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            double dx = static_cast<double>(x);
            double dy = static_cast<double>(y);
            double dz = static_cast<double>(block.get(x, y));
            
            sum_x += dx;
            sum_y += dy;
            sum_z += dz;
            sum_xx += dx * dx;
            sum_yy += dy * dy;
            sum_xy += dx * dy;
            sum_xz += dx * dz;
            sum_yz += dy * dz;
        }
    }
    
    double mean_x = sum_x / n;
    double mean_y = sum_y / n;
    double mean_z = sum_z / n;
    
    double s_xx = sum_xx - sum_x * sum_x / n;
    double s_yy = sum_yy - sum_y * sum_y / n;
    double s_xy = sum_xy - sum_x * sum_y / n;
    
    double s_xz = sum_xz - sum_x * sum_z / n;
    double s_yz = sum_yz - sum_y * sum_z / n;
    
    int32_t a_fixed = 0;
    int32_t b_fixed = 0;
    
    // determinant
    double det = s_xx * s_yy - s_xy * s_xy;
    if (det > 1e-5) {
        double a = (s_yy * s_xz - s_xy * s_yz) / det;
        double b = (s_xx * s_yz - s_xy * s_xz) / det;
        a_fixed = static_cast<int32_t>(std::round(a * 1024.0));
        b_fixed = static_cast<int32_t>(std::round(b * 1024.0));
    }
    
    int32_t c_fixed = static_cast<int32_t>(std::round((mean_z - (a_fixed/1024.0)*mean_x - (b_fixed/1024.0)*mean_y) * 1024.0));
    
    result.parameters = {a_fixed, b_fixed, c_fixed};
    
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t X = block.get(x, y);
            int32_t P = (a_fixed * static_cast<int32_t>(x) + b_fixed * static_cast<int32_t>(y) + c_fixed) / 1024;
            result.residuals.push_back(X - P);
        }
    }
    
    return result;
}

void PlanePredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    int32_t a_fixed = encoded.parameters[0];
    int32_t b_fixed = encoded.parameters[1];
    int32_t c_fixed = encoded.parameters[2];
    
    for (std::uint32_t y = 0; y < block.height; ++y) {
        for (std::uint32_t x = 0; x < block.width; ++x) {
            int32_t R = encoded.residuals[y * block.width + x];
            int32_t P = (a_fixed * static_cast<int32_t>(x) + b_fixed * static_cast<int32_t>(y) + c_fixed) / 1024;
            block.set(x, y, R + P);
        }
    }
}

} // namespace xtm::predictor
