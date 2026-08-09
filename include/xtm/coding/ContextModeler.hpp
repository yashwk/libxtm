#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <array>
#include "xtm/coding/RangeCoder.hpp"

namespace xtm::coding {

enum class ContextStream : uint8_t {
    Meter = 0,
    Precision = 1
};

struct Context {
    uint8_t stream; // 0=Meter, 1=Precision
    uint8_t neighbour_activity; // 0=low variance, 1=high variance
    
    bool operator==(const Context& other) const {
        return stream == other.stream && neighbour_activity == other.neighbour_activity;
    }
};

} // namespace xtm::coding

namespace std {
    template<>
    struct hash<xtm::coding::Context> {
        size_t operator()(const xtm::coding::Context& ctx) const {
            return (std::hash<uint8_t>()(ctx.stream) << 1) ^ 
                   std::hash<uint8_t>()(ctx.neighbour_activity);
        }
    };
}

namespace xtm::coding {

enum class ContextModel {
    Simple,
    Extended
};

// Maximum wavelet decomposition levels for a block of the given dimensions.
inline uint32_t max_wavelet_levels(uint32_t width, uint32_t height) {
    uint32_t levels = 3;
    uint32_t dim = std::min(width, height);
    while (levels > 0 && dim < (1u << levels)) {
        levels--;
    }
    return levels;
}

inline uint8_t get_context_index(const Context& ctx) {
    return (ctx.stream << 1) | ctx.neighbour_activity;
}

struct EncodingContext {
    std::array<FrequencyTable, 4> tables = {
        FrequencyTable(33), FrequencyTable(33), FrequencyTable(33), FrequencyTable(33)
    };
    FrequencyTable run_table{256};
    FrequencyTable uniform_bit{2};
    
    void reset() {
        for (auto& t : tables) t.reset();
        run_table.reset();
        uniform_bit.reset();
    }
};

void encode_stream(const std::vector<int32_t>& data, uint32_t width, uint32_t height, ContextModel model, bool has_precision, class ArithmeticEncoder& ac, EncodingContext& ctx);
void decode_stream(std::vector<int32_t>& data, uint32_t width, uint32_t height, ContextModel model, bool has_precision, class ArithmeticDecoder& ad, EncodingContext& ctx);
void analyze_symbols(const std::vector<int32_t>& data, uint32_t width, uint32_t height, ContextModel model, bool has_precision, std::vector<int32_t>& mag_classes, std::vector<int32_t>& run_lengths, std::unordered_map<Context, uint32_t>& context_sizes, uint32_t& remainder_bits);

} // namespace xtm::coding
