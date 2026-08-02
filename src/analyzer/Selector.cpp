#include "xtm/analyzer/Selector.hpp"
#include "xtm/analyzer/Statistics.hpp"
#include "xtm/transform/Wavelet.hpp"
#include <limits>
#include <algorithm>

namespace xtm::analyzer {

PredictorSelector::PredictorSelector(const std::vector<const predictor::Predictor*>& predictors, double early_exit_threshold, PipelineOrder pipeline_order)
    : predictors_(predictors), early_exit_threshold_(early_exit_threshold), pipeline_order_(pipeline_order) {}

SelectionResult PredictorSelector::select(const partition::BlockView& block) const {
    SelectionResult best_result;
    best_result.best_predictor = nullptr;
    best_result.total_bits = std::numeric_limits<double>::infinity();
    
    int num_samples = block.width * block.height;
    
    // Quick Terrain Classification (V12.1)
    int32_t min_val = block.row_data(0)[0];
    int32_t max_val = min_val;
    for (uint32_t y = 0; y < block.height; ++y) {
        const int32_t* row = block.row_data(y);
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t val = row[x];
            if (val < min_val) min_val = val;
            if (val > max_val) max_val = val;
        }
    }
    int32_t delta = max_val - min_val;
    
    std::vector<const predictor::Predictor*> active_predictors;
    if (delta == 0) {
        // Perfectly flat terrain: Prune Gradient, Plane, JPEG-LS
        for (auto* p : predictors_) {
            std::string n = p->name();
            if (n == "Average" || n == "Left" || n == "Above") {
                active_predictors.push_back(p);
            }
        }
    } else if (delta > 200) {
        // Very noisy terrain: Prune Plane and JPEG-LS
        for (auto* p : predictors_) {
            std::string n = p->name();
            if (n != "Plane" && n != "JPEG-LS") {
                active_predictors.push_back(p);
            }
        }
    } else {
        active_predictors = predictors_;
    }
    
    if (active_predictors.empty()) {
        active_predictors = predictors_; // Fallback
    }
    
    if (pipeline_order_ == PipelineOrder::PredictorWavelet) {
        for (const auto* pred : active_predictors) {
            predictor::PredictionResult encoded;
            pred->encode(block, encoded);
            
            double entropy = calculate_entropy(encoded.residuals);
            
            double c_id = 8.0; // 1 byte for predictor ID
            double c_params = encoded.parameters.size() * 32.0; // 32 bits per parameter
            double c_residual = entropy * num_samples;
            
            double total_bits = c_id + c_params + c_residual;
            
            if (total_bits < best_result.total_bits) {
                best_result.total_bits = total_bits;
                best_result.best_predictor = pred;
                best_result.best_encoded = std::move(encoded);
            }
        }
        
        // Evaluate Wavelet on the best predictor's residuals
        uint32_t max_levels = 3;
        uint32_t dim = std::min(block.width, block.height);
        while (max_levels > 0 && dim < (1u << max_levels)) {
            max_levels--;
        }
        
        if (max_levels > 0) {
            transform::CDF53Transform::forward_2d(best_result.best_encoded.residuals, block.width, block.height, max_levels);
            double wv_entropy = calculate_entropy(best_result.best_encoded.residuals);
            double c_residual_wv = wv_entropy * num_samples;
            
            double wv_total_bits = 8.0 + (best_result.best_encoded.parameters.size() * 32.0) + c_residual_wv + 1.0; // +1 bit for wavelet flag
            
            if (wv_total_bits < best_result.total_bits) {
                best_result.total_bits = wv_total_bits;
                best_result.use_wavelet = true;
                best_result.wavelet_levels = max_levels;
            } else {
                best_result.use_wavelet = false;
                best_result.wavelet_levels = 0;
                // Add the 1-bit flag overhead to the non-wavelet decision
                best_result.total_bits += 1.0;
                // Revert wavelet transform
                transform::CDF53Transform::inverse_2d(best_result.best_encoded.residuals, block.width, block.height, max_levels);
            }
        } else {
            best_result.use_wavelet = false;
            best_result.wavelet_levels = 0;
            best_result.total_bits += 1.0;
        }
    } else {
        // PipelineOrder::WaveletPredictor
        uint32_t max_levels = 3;
        uint32_t dim = std::min(block.width, block.height);
        while (max_levels > 0 && dim < (1u << max_levels)) {
            max_levels--;
        }
        
        std::vector<int32_t> wv_data(num_samples);
        for (uint32_t y = 0; y < block.height; ++y) {
            for (uint32_t x = 0; x < block.width; ++x) {
                wv_data[y * block.width + x] = block.get(x, y);
            }
        }
        
        if (max_levels > 0) {
            transform::CDF53Transform::forward_2d(wv_data, block.width, block.height, max_levels);
        }
        
        terrain::IntGrid wv_grid;
        wv_grid.width = block.width;
        wv_grid.height = block.height;
        wv_grid.data = wv_data;
        
        partition::BlockView wv_block;
        wv_block.grid = &wv_grid;
        wv_block.x_offset = 0;
        wv_block.y_offset = 0;
        wv_block.width = block.width;
        wv_block.height = block.height;
        
        for (const auto* pred : active_predictors) {
            predictor::PredictionResult encoded;
            pred->encode(wv_block, encoded);
            
            double entropy = calculate_entropy(encoded.residuals);
            
            double c_id = 8.0; 
            double c_params = encoded.parameters.size() * 32.0;
            double c_residual = entropy * num_samples;
            
            double total_bits = c_id + c_params + c_residual + 1.0; // +1 bit for wavelet
            
            if (total_bits < best_result.total_bits) {
                best_result.total_bits = total_bits;
                best_result.best_predictor = pred;
                best_result.best_encoded = std::move(encoded);
                best_result.use_wavelet = (max_levels > 0);
                best_result.wavelet_levels = max_levels;
            }
        }
    }
    
    best_result.bits_per_sample = best_result.total_bits / num_samples;
    return best_result;
}

} // namespace xtm::analyzer
