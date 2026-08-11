#include "xtm/predictor/Predictors.hpp"
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace xtm::predictor {

// 3x3 determinant
static double det3(double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

void LeastSquaresPredictor::encode(const partition::BlockView& block, std::vector<int32_t>& residuals, std::vector<int32_t>& parameters) const {
    residuals.assign(block.width * block.height, 0);
    parameters.clear();
    
    // We will fit weights w1, w2, w3 for W, N, NW
    // P = w1*W + w2*N + w3*NW
    
    double A[3][3] = {{0}};
    double B[3] = {0};
    
    for (uint32_t y = 1; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = block.row_data(y - 1);
        for (uint32_t x = 1; x < block.width; ++x) {
            double X = static_cast<double>(row[x]);
            double W = static_cast<double>(row[x - 1]);
            double N = static_cast<double>(above[x]);
            double NW = static_cast<double>(above[x - 1]);
            
            double C[3] = {W, N, NW};
            
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    A[i][j] += C[i] * C[j];
                }
                B[i] += C[i] * X;
            }
        }
    }
    
    // Solve Aw = B using Cramer's rule
    double detA = det3(A);
    double w1 = 0, w2 = 0, w3 = 0;
    
    if (std::abs(detA) > 1e-5) {
        double Ax[3][3], Ay[3][3], Az[3][3];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                Ax[i][j] = (j == 0) ? B[i] : A[i][j];
                Ay[i][j] = (j == 1) ? B[i] : A[i][j];
                Az[i][j] = (j == 2) ? B[i] : A[i][j];
            }
        }
        w1 = det3(Ax) / detA;
        w2 = det3(Ay) / detA;
        w3 = det3(Az) / detA;
    } else {
        // Fallback to gradient predictor weights if matrix is singular (e.g. flat region)
        w1 = 1.0;
        w2 = 1.0;
        w3 = -1.0;
    }
    
    // Quantize weights (e.g., 8 fractional bits -> 256)
    int32_t qw1 = static_cast<int32_t>(std::round(w1 * 256.0));
    int32_t qw2 = static_cast<int32_t>(std::round(w2 * 256.0));
    int32_t qw3 = static_cast<int32_t>(std::round(w3 * 256.0));
    
    parameters = {qw1, qw2, qw3};
    
    // First row (y == 0): p = row[x - 1] (or 0 at x == 0).
    residuals[0] = block.row_data(0)[0];
    {
        const int32_t* row = block.row_data(0);
        for (uint32_t x = 1; x < block.width; ++x) {
            residuals[x] = row[x] - row[x - 1];
        }
    }
    
    // First column (x == 0): p = above[0].
    for (uint32_t y = 1; y < block.height; ++y) {
        residuals[y * block.width] = block.row_data(y)[0] - block.row_data(y - 1)[0];
    }
    
    // Interior: reads only original row/above data (no loop-carried
    // dependency), vectorizer-friendly.
    const uint32_t w = block.width;
    const uint32_t h = block.height;
    for (uint32_t y = 1; y < h; ++y) {
        const int32_t* row = block.row_data(y);
        const int32_t* above = block.row_data(y - 1);
#if defined(__clang__)
#pragma clang loop vectorize(enable)
#else
#pragma GCC ivdep
#endif
        for (uint32_t x = 1; x < w; ++x) {
            int32_t W = row[x - 1];
            int32_t N = above[x];
            int32_t NW = above[x - 1];
            residuals[y * w + x] = row[x] - ((qw1 * W + qw2 * N + qw3 * NW) / 256);
        }
    }
    
    return;
}

void LeastSquaresPredictor::decode(const std::vector<int32_t>& residuals, const std::vector<int32_t>& parameters, partition::MutableBlockView& block) const {
    if (parameters.size() < 3) {
        throw std::runtime_error("Corrupt XTM: Least Squares predictor requires 3 parameters, got "
                                 + std::to_string(parameters.size()));
    }
    int32_t qw1 = parameters[0];
    int32_t qw2 = parameters[1];
    int32_t qw3 = parameters[2];
    
    size_t i = 0;
    for (uint32_t y = 0; y < block.height; ++y) {
        int32_t* row = block.row_data(y);
        const int32_t* above = (y > 0) ? block.row_data(y - 1) : nullptr;
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t res = residuals[i++];
            int32_t p = 0;
            
            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                p = row[x - 1];
            } else if (x == 0) {
                p = above[0];
            } else {
                int32_t W = row[x - 1];
                int32_t N = above[x];
                int32_t NW = above[x - 1];
                
                p = (qw1 * W + qw2 * N + qw3 * NW) / 256;
            }
            
            row[x] = p + res;
        }
    }
}

} // namespace xtm::predictor
