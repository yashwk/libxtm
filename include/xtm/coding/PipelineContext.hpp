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

// Forward symbol walker shared by encode_stream, decode_stream and the
// selection estimator: splits the block into (meter, precision) streams,
// computes per-sample contexts, zigzag + magnitude-class symbols and zero
// runs (capped at 255). Emits (context_index, mag, run, remainder) events:
// run > 0 means a run of `run` zeros (mag == 0); otherwise a nonzero symbol
// with magnitude class `mag` and `mag - 1` remainder bits.
template <typename Emit>
inline void walk_symbols(const std::vector<int32_t>& data, uint32_t width, uint32_t height,
                         const PipelineContext& pctx, Emit&& emit) {
    const uint32_t length = width * height;
    const auto process_stream = [&](uint32_t start_idx, uint8_t stream) {
        uint32_t zero_run = 0;
        uint8_t run_ctx = 0;
        for (uint32_t i = 0; i < length; ++i) {
            int32_t val = data[start_idx + i];
            uint8_t ctx = context_index(
                stream, stream_activity(pctx.context_model,
                                        data, start_idx, i, width));
            if (val == 0) {
                if (zero_run == 0) run_ctx = ctx;
                zero_run++;
                if (zero_run == 255 || i == length - 1) {
                    emit(run_ctx, static_cast<uint8_t>(0), zero_run, static_cast<uint32_t>(0));
                    zero_run = 0;
                }
            } else {
                if (zero_run > 0) {
                    emit(run_ctx, static_cast<uint8_t>(0), zero_run, static_cast<uint32_t>(0));
                    zero_run = 0;
                }
                uint32_t zz = zigzag_encode(val);
                uint32_t mag = get_magnitude_class(zz);
                uint32_t remainder = (mag > 1) ? (zz & ((1u << (mag - 1)) - 1)) : 0;
                emit(ctx, static_cast<uint8_t>(mag), static_cast<uint32_t>(0), remainder);
            }
        }
    };
    process_stream(0, 0);
    if (pctx.has_precision) {
        process_stream(length, 1);
    }
}

} // namespace xtm::coding
