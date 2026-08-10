#pragma once
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/analyzer/PipelineType.hpp"
#include <cstdint>
#include <cmath>

namespace xtm::coding {

struct PipelineContext {
    double precision = 1.0;
    ContextModel context_model = ContextModel::Simple;
    analyzer::PipelineType pipeline_type = analyzer::PipelineType::Predictor;
    bool disable_quadtree = false;
    uint32_t num_threads = 0; // 0 = hardware_concurrency
    
    // Explicit split-precision state
    bool has_precision = false;
    int32_t precision_multiplier = 1;

    PipelineContext() = default;

    PipelineContext(double precision_, ContextModel model_ = ContextModel::Simple, 
                    analyzer::PipelineType type_ = analyzer::PipelineType::Predictor, 
                    bool disable_quadtree_ = false, uint32_t num_threads_ = 0)
        : precision(precision_), context_model(model_), pipeline_type(type_), 
          disable_quadtree(disable_quadtree_), num_threads(num_threads_) {
        has_precision = (precision < 1.0);
        precision_multiplier = has_precision ? static_cast<int32_t>(std::round(1.0 / precision)) : 1;
    }
};

} // namespace xtm::coding
