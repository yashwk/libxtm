#include "xtm/predictor/Predictors.hpp"
#include <cmath>
#include <vector>

namespace xtm::predictor {

// 3x3 determinant
static double det3(double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

PredictionResult LeastSquaresPredictor::encode(const partition::BlockView& block) const {
    PredictionResult result;
    result.residuals.reserve(block.width * block.height);
    
    // We will fit weights w1, w2, w3 for W, N, NW
    // P = w1*W + w2*N + w3*NW
    
    double A[3][3] = {0};
    double B[3] = {0};
    
    for (uint32_t y = 1; y < block.height; ++y) {
        for (uint32_t x = 1; x < block.width; ++x) {
            double X = static_cast<double>(block.get(x, y));
            double W = static_cast<double>(block.get(x-1, y));
            double N = static_cast<double>(block.get(x, y-1));
            double NW = static_cast<double>(block.get(x-1, y-1));
            
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
    
    result.parameters = {qw1, qw2, qw3};
    
    for (uint32_t y = 0; y < block.height; ++y) {
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t val = block.get(x, y);
            int32_t p = 0;
            
            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                p = block.get(x - 1, 0);
            } else if (x == 0) {
                p = block.get(0, y - 1);
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t NW = block.get(x - 1, y - 1);
                
                p = (qw1 * W + qw2 * N + qw3 * NW) / 256;
            }
            
            result.residuals.push_back(val - p);
        }
    }
    
    return result;
}

void LeastSquaresPredictor::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    int32_t qw1 = 256, qw2 = 256, qw3 = -256;
    if (encoded.parameters.size() >= 3) {
        qw1 = encoded.parameters[0];
        qw2 = encoded.parameters[1];
        qw3 = encoded.parameters[2];
    }
    
    size_t i = 0;
    for (uint32_t y = 0; y < block.height; ++y) {
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t res = encoded.residuals[i++];
            int32_t p = 0;
            
            if (x == 0 && y == 0) {
                p = 0;
            } else if (y == 0) {
                p = block.get(x - 1, 0);
            } else if (x == 0) {
                p = block.get(0, y - 1);
            } else {
                int32_t W = block.get(x - 1, y);
                int32_t N = block.get(x, y - 1);
                int32_t NW = block.get(x - 1, y - 1);
                
                p = (qw1 * W + qw2 * N + qw3 * NW) / 256;
            }
            
            block.set(x, y, p + res);
        }
    }
}

} // namespace xtm::predictor
