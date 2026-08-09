#include "xtm/predictor/SplitPrecisionWrapper.hpp"

namespace xtm::predictor {

SplitPrecisionWrapper::SplitPrecisionWrapper(int32_t precision_multiplier, const Predictor* meter_predictor, const Predictor* precision_predictor)
    : precision_multiplier_(precision_multiplier), meter_predictor_(meter_predictor), precision_predictor_(precision_predictor) {}

void SplitPrecisionWrapper::encode(const partition::BlockView& block, PredictionResult& result) const {
    // We need to run meter_predictor on M and precision_predictor on P.
    // To allow predictors to read their neighbors (left, top, top-left), we must copy 
    // the block and a 1-pixel border into temporary IntGrids.
    
    terrain::IntGrid temp_meter;
    temp_meter.width = block.grid->width;
    temp_meter.height = block.grid->height;
    temp_meter.data.resize(temp_meter.width * temp_meter.height, 0);
    
    terrain::IntGrid temp_prec;
    temp_prec.width = block.grid->width;
    temp_prec.height = block.grid->height;
    temp_prec.data.resize(temp_prec.width * temp_prec.height, 0);
    
    // Determine the bounding box of what we need (the block itself, plus up to 1 row above and 1 col left)
    uint32_t start_x = (block.x_offset > 0) ? block.x_offset - 1 : 0;
    uint32_t start_y = (block.y_offset > 0) ? block.y_offset - 1 : 0;
    uint32_t end_x = block.x_offset + block.width;
    uint32_t end_y = block.y_offset + block.height;
    
    for (uint32_t y = start_y; y < end_y; ++y) {
        for (uint32_t x = start_x; x < end_x; ++x) {
            int32_t z = block.grid->get(x, y);
            int32_t m = z / precision_multiplier_;
            int32_t p = z % precision_multiplier_;
            uint32_t idx = y * temp_meter.width + x;
            temp_meter.data[idx] = m;
            temp_prec.data[idx] = p;
        }
    }
    
    partition::BlockView m_view = block;
    m_view.grid = &temp_meter;
    
    partition::BlockView p_view = block;
    p_view.grid = &temp_prec;
    
    PredictionResult m_result;
    meter_predictor_->encode(m_view, m_result);
    
    PredictionResult p_result;
    precision_predictor_->encode(p_view, p_result);
    
    result.residuals = std::move(m_result.residuals);
    result.residuals.insert(result.residuals.end(), p_result.residuals.begin(), p_result.residuals.end());
    
    result.parameters = std::move(m_result.parameters);
    result.parameters.insert(result.parameters.end(), p_result.parameters.begin(), p_result.parameters.end());
}

void SplitPrecisionWrapper::decode(const PredictionResult& encoded, partition::MutableBlockView& block) const {
    // The decoder gives us the combined residuals [Meter..., Precision...]
    // We must split them, decode M, decode P, and combine into Z.
    
    uint32_t num_pixels = block.width * block.height;
    
    PredictionResult m_encoded;
    m_encoded.residuals.assign(encoded.residuals.begin(), encoded.residuals.begin() + num_pixels);
    
    PredictionResult p_encoded;
    p_encoded.residuals.assign(encoded.residuals.begin() + num_pixels, encoded.residuals.end());
    
    // We don't know exactly how many parameters each predictor uses, but we can assume
    // that the meter predictor takes the first N, and precision takes the rest.
    // For now, we pass all parameters to both; the predictors only read what they need.
    m_encoded.parameters = encoded.parameters;
    p_encoded.parameters = encoded.parameters;
    // Note: If meter and precision BOTH have parameters, we need to know the offset. 
    // In our current design, precision predictor is usually Left or Gradient which have 0 params.
    // If precision uses Polynomial, we would need to split parameters properly. 
    // For safety, we assume only the meter predictor has parameters.
    
    terrain::IntGrid temp_meter;
    temp_meter.width = block.grid->width;
    temp_meter.height = block.grid->height;
    temp_meter.data.resize(temp_meter.width * temp_meter.height, 0);
    
    terrain::IntGrid temp_prec;
    temp_prec.width = block.grid->width;
    temp_prec.height = block.grid->height;
    temp_prec.data.resize(temp_prec.width * temp_prec.height, 0);
    
    // Copy context from the main grid into temp grids
    uint32_t start_x = (block.x_offset > 0) ? block.x_offset - 1 : 0;
    uint32_t start_y = (block.y_offset > 0) ? block.y_offset - 1 : 0;
    
    for (uint32_t y = start_y; y < block.y_offset + block.height; ++y) {
        for (uint32_t x = start_x; x < block.x_offset + block.width; ++x) {
            // Only copy context, not the block itself (which is uninitialized)
            if (y >= block.y_offset && x >= block.x_offset) continue;
            int32_t z = block.grid->get(x, y);
            temp_meter.data[y * temp_meter.width + x] = z / precision_multiplier_;
            temp_prec.data[y * temp_prec.width + x] = z % precision_multiplier_;
        }
    }
    
    partition::MutableBlockView m_view = block;
    m_view.grid = &temp_meter;
    
    partition::MutableBlockView p_view = block;
    p_view.grid = &temp_prec;
    
    meter_predictor_->decode(m_encoded, m_view);
    precision_predictor_->decode(p_encoded, p_view);
    
    // Combine back into Z
    for (uint32_t y = 0; y < block.height; ++y) {
        for (uint32_t x = 0; x < block.width; ++x) {
            int32_t m = m_view.get(x, y);
            int32_t p = p_view.get(x, y);
            block.set(x, y, m * precision_multiplier_ + p);
        }
    }
}

} // namespace xtm::predictor
