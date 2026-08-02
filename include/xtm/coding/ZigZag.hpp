#pragma once
#include <cstdint>

namespace xtm::coding {

// Encodes signed 32-bit integer to unsigned 32-bit integer.
// 0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3, 2 -> 4, etc.
inline uint32_t zigzag_encode(int32_t val) {
    return (static_cast<uint32_t>(val) << 1) ^ static_cast<uint32_t>(val >> 31);
}

// Decodes unsigned 32-bit integer back to signed 32-bit integer.
inline int32_t zigzag_decode(uint32_t val) {
    return static_cast<int32_t>((val >> 1) ^ (0 - (val & 1)));
}

} // namespace xtm::coding
