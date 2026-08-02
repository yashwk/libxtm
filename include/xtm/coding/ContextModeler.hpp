#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace xtm::coding {

enum class Subband : uint8_t {
    LL = 0,
    LH = 1,
    HL = 2,
    HH = 3
};

struct Context {
    uint8_t subband; // 0=LL, 1=LH, 2=HL, 3=HH
    uint8_t neighbour_activity; // 0=low variance, 1=high variance
    
    bool operator==(const Context& other) const {
        return subband == other.subband && neighbour_activity == other.neighbour_activity;
    }
};

} // namespace xtm::coding

namespace std {
    template<>
    struct hash<xtm::coding::Context> {
        size_t operator()(const xtm::coding::Context& ctx) const {
            return (std::hash<uint8_t>()(ctx.subband) << 1) ^ 
                   std::hash<uint8_t>()(ctx.neighbour_activity);
        }
    };
}

namespace xtm::coding {

enum class ContextModel {
    Simple,
    Extended
};

struct Symbol {
    uint32_t magnitude_class;
    uint32_t remainder;
    uint32_t run_length; // Only valid if magnitude_class == 0
    Context context;
};

// Generates the sequence of symbols and contexts from a wavelet block
std::vector<Symbol> generate_symbols(const std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t max_levels, ContextModel model = ContextModel::Extended);

// Reconstructs the wavelet block from the sequence of symbols
void reconstruct_symbols(std::vector<int32_t>& data, uint32_t width, uint32_t height, uint32_t max_levels, const std::vector<Symbol>& symbols);

} // namespace xtm::coding
