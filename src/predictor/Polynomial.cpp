#include "xtm/predictor/Predictors.hpp"
#include "xtm/analyzer/Statistics.hpp"
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

namespace xtm::predictor {

namespace {

    struct Matrix {
        int rows, cols;
        std::vector<double> data;
        Matrix(int r, int c) : rows(r), cols(c), data(r * c, 0.0) {}
        double& operator()(int r, int c) { return data[r * cols + c]; }
        const double& operator()(int r, int c) const { return data[r * cols + c]; }
    };

    bool solve_linear_system(Matrix& A, std::vector<double>& b, std::vector<double>& x) {
        int n = A.rows;
        for (int i = 0; i < n; i++) {
            int max_row = i;
            double max_val = std::abs(A(i, i));
            for (int k = i + 1; k < n; k++) {
                if (std::abs(A(k, i)) > max_val) {
                    max_val = std::abs(A(k, i));
                    max_row = k;
                }
            }
            if (max_val < 1e-9) return false;
            
            if (i != max_row) {
                for (int j = i; j < n; j++) std::swap(A(i, j), A(max_row, j));
                std::swap(b[i], b[max_row]);
            }
            
            for (int k = i + 1; k < n; k++) {
                double factor = A(k, i) / A(i, i);
                for (int j = i; j < n; j++) {
                    A(k, j) -= factor * A(i, j);
                }
                b[k] -= factor * b[i];
            }
        }
        
        x.assign(n, 0.0);
        for (int i = n - 1; i >= 0; i--) {
            double sum = 0;
            for (int j = i + 1; j < n; j++) sum += A(i, j) * x[j];
            x[i] = (b[i] - sum) / A(i, i);
        }
        return true;
    }

    constexpr int U_DEG[10] = {0, 1, 0, 2, 1, 0, 3, 2, 1, 0};
    constexpr int V_DEG[10] = {0, 0, 1, 0, 1, 2, 0, 1, 2, 3};
    constexpr int64_t SCALE = 16384;

    int64_t compute_term(int k, int64_t u, int64_t v) {
        int64_t res = 1;
        for (int i = 0; i < U_DEG[k]; ++i) res *= u;
        for (int i = 0; i < V_DEG[k]; ++i) res *= v;
        return res;
    }
}

void PolynomialPredictor::encode(const partition::BlockView& block, std::vector<int32_t>& residuals, std::vector<int32_t>& parameters) const {
    residuals.clear();
    parameters.clear();
    
    int w = block.width;
    int h = block.height;
    int num_samples = w * h;
    
    int64_t u_scale = std::max(1, w - 1);
    int64_t v_scale = std::max(1, h - 1);
    
    // Test orders 1, 2, 3
    int order_params[3] = {3, 6, 10};
    
    std::vector<int32_t> best_res_residuals;
    std::vector<int32_t> best_res_parameters;
    double best_cost = std::numeric_limits<double>::infinity();
    
    for (int o = 0; o < 3; ++o) {
        int k_params = order_params[o];
        Matrix A(k_params, k_params);
        std::vector<double> B(k_params, 0.0);
        
        for (int y = 0; y < h; ++y) {
            const int32_t* row = block.row_data(y);
            int64_t v = 2 * (int64_t)y - (h - 1);
            for (int x = 0; x < w; ++x) {
                int64_t u = 2 * (int64_t)x - (w - 1);
                double z = row[x];
                
                double f[10];
                for (int k = 0; k < k_params; ++k) {
                    double norm_u = static_cast<double>(u) / u_scale;
                    double norm_v = static_cast<double>(v) / v_scale;
                    double val = 1.0;
                    for (int i = 0; i < U_DEG[k]; ++i) val *= norm_u;
                    for (int i = 0; i < V_DEG[k]; ++i) val *= norm_v;
                    f[k] = val;
                }
                
                for (int r = 0; r < k_params; ++r) {
                    B[r] += f[r] * z;
                    for (int c = 0; c < k_params; ++c) {
                        A(r, c) += f[r] * f[c];
                    }
                }
            }
        }
        
        std::vector<double> C;
        if (!solve_linear_system(A, B, C)) {
            continue; // Singular matrix, skip this order
        }
        
        std::vector<int32_t> curr_res_residuals;
        std::vector<int32_t> curr_res_parameters;
        curr_res_residuals.reserve(num_samples);
        curr_res_parameters.push_back(o + 1); // Parameter 0 is the order (1, 2, or 3)
        
        for (int k = 0; k < k_params; ++k) {
            double scaled_c = C[k] * SCALE;
            scaled_c = std::max(-2147483600.0, std::min(2147483600.0, scaled_c));
            curr_res_parameters.push_back(static_cast<int32_t>(std::round(scaled_c)));
        }
        
        for (int y = 0; y < h; ++y) {
            const int32_t* row = block.row_data(y);
            int64_t v = 2 * (int64_t)y - (h - 1);
            for (int x = 0; x < w; ++x) {
                int64_t u = 2 * (int64_t)x - (w - 1);
                
                int64_t p_val = 0;
                for (int k = 0; k < k_params; ++k) {
                    int64_t term = compute_term(k, u, v);
                    int64_t c_fixed = curr_res_parameters[k + 1];
                    
                    int64_t denom = SCALE;
                    for (int i = 0; i < U_DEG[k]; ++i) denom *= u_scale;
                    for (int i = 0; i < V_DEG[k]; ++i) denom *= v_scale;
                    
                    p_val += (c_fixed * term) / denom;
                }
                
                curr_res_residuals.push_back(row[x] - static_cast<int32_t>(p_val));
            }
        }
        
        double entropy = analyzer::calculate_entropy(curr_res_residuals);
        double cost = entropy * num_samples + curr_res_parameters.size() * 32.0;
        
        if (cost < best_cost) {
            best_cost = cost;
            best_res_residuals = std::move(curr_res_residuals);
            best_res_parameters = std::move(curr_res_parameters);
        }
    }
    
    if (best_cost == std::numeric_limits<double>::infinity()) {
        // Fallback to order 1 with 0 parameters if solving failed completely
        best_res_parameters = {1, 0, 0, 0};
        best_res_residuals.assign(num_samples, 0);
        for (int y = 0; y < h; ++y) {
            const int32_t* row = block.row_data(y);
            for (int x = 0; x < w; ++x) {
                best_res_residuals[y * w + x] = row[x];
            }
        }
    }
    
    residuals = std::move(best_res_residuals);
    parameters = std::move(best_res_parameters);
}

void PolynomialPredictor::decode(const std::vector<int32_t>& residuals, const std::vector<int32_t>& parameters, partition::MutableBlockView& block) const {
    if (parameters.empty()) {
        throw std::runtime_error("Corrupt XTM: Polynomial predictor missing order parameter");
    }
    
    int order = parameters[0];
    int k_params = 0;
    if (order == 1) k_params = 3;
    else if (order == 2) k_params = 6;
    else if (order == 3) k_params = 10;
    else throw std::runtime_error("Corrupt XTM: Invalid polynomial order");
    
    if (parameters.size() < static_cast<size_t>(k_params + 1)) {
        throw std::runtime_error("Corrupt XTM: Polynomial predictor missing coefficients");
    }
    
    int w = block.width;
    int h = block.height;
    size_t i = 0;
    
    int64_t u_scale = std::max(1, w - 1);
    int64_t v_scale = std::max(1, h - 1);
    
    for (int y = 0; y < h; ++y) {
        int32_t* row = block.row_data(y);
        int64_t v = 2 * (int64_t)y - (h - 1);
        for (int x = 0; x < w; ++x) {
            int64_t u = 2 * (int64_t)x - (w - 1);
            
            int64_t p_val = 0;
            for (int k = 0; k < k_params; ++k) {
                int64_t term = compute_term(k, u, v);
                int64_t c_fixed = parameters[k + 1];
                
                int64_t denom = SCALE;
                for (int d = 0; d < U_DEG[k]; ++d) denom *= u_scale;
                for (int d = 0; d < V_DEG[k]; ++d) denom *= v_scale;
                
                p_val += (c_fixed * term) / denom;
            }
            
            row[x] = residuals[i++] + static_cast<int32_t>(p_val);
        }
    }
}

} // namespace xtm::predictor
