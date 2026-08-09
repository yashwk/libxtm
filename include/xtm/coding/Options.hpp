#pragma once
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/analyzer/Selector.hpp"
#include <cstdint>

namespace xtm::coding {

struct Options {
    float scale = 1.0f;
    ContextModel context_model = ContextModel::Simple;
    analyzer::PipelineType pipeline_type = analyzer::PipelineType::Predictor;
    bool disable_quadtree = false;
    uint32_t num_threads = 0; // 0 = hardware_concurrency
};

} // namespace xtm::coding
