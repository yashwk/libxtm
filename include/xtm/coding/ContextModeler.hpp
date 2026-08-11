#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <array>
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"

namespace xtm::coding {

enum class ContextStream : uint8_t {
    Meter = 0,
    Precision = 1
};

enum class ContextModel {
    Simple,
    Extended
};

struct PipelineContext;

// Maximum wavelet decomposition levels for a block of the given dimensions.
inline uint32_t max_wavelet_levels(uint32_t width, uint32_t height) {
    uint32_t levels = 3;
    uint32_t dim = std::min(width, height);
    while (levels > 0 && dim < (1u << levels)) {
        levels--;
    }
    return levels;
}

// Overflow-safe absolute value of an int32 (well-defined for INT32_MIN).
inline uint32_t safe_abs(int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    return (v < 0) ? (0u - u) : u;
}

// Quantized local-activity level of a magnitude: 0 (<= 2), 1 (<= 8),
// 2 (<= 32), 3 (> 32). Monotone in |v|, so it composes with max().
inline uint8_t activity_bucket(uint32_t magnitude) {
    if (magnitude > 32) return 3;
    if (magnitude > 8) return 2;
    if (magnitude > 2) return 1;
    return 0;
}

// Neighbour-activity levels per stream: 4 levels (2-bit activity bucket).
inline uint8_t activity_levels() {
    return 4;
}

// Flat table index for (stream, activity): 8 slots (2 streams x 4 levels).
inline uint8_t context_index(uint8_t stream, uint8_t activity) {
    return static_cast<uint8_t>(stream * activity_levels() + activity);
}

// Activity level for the sample at position `i` within a stream whose first
// element is at data[start_idx]. `width` is the stream's row width.
// Extended: 2-bit bucket over the west/north neighbours (the same geometry
// the second-order residual pass uses). Simple: always 0.
inline uint8_t stream_activity(ContextModel context_model,
                               const std::vector<int32_t>& data, uint32_t start_idx,
                               uint32_t i, uint32_t width) {
    if (context_model != ContextModel::Extended) return 0;
    uint32_t x = i % width;
    uint32_t y = i / width;
    int32_t w_val = (x > 0) ? data[start_idx + i - 1] : 0;
    int32_t n_val = (y > 0) ? data[start_idx + i - width] : 0;
    uint8_t aw = activity_bucket(safe_abs(w_val));
    uint8_t an = activity_bucket(safe_abs(n_val));
    return std::max(aw, an);
}

// Forward symbol walker shared by encode_stream, decode_stream and the
// selection estimator (defined in PipelineContext.hpp, after the struct).

struct EncodingContext {
    explicit EncodingContext();

    std::array<FrequencyTable, 8> tables;
    FrequencyTable run_table;
    FrequencyTable uniform_bit;

    void reset();
};

void encode_stream(const std::vector<int32_t>& data, uint32_t width, uint32_t height, const PipelineContext& pctx, class ArithmeticEncoder& ac, EncodingContext& ectx);
void decode_stream(std::vector<int32_t>& data, uint32_t width, uint32_t height, const PipelineContext& pctx, class ArithmeticDecoder& ad, EncodingContext& ectx);

} // namespace xtm::coding
