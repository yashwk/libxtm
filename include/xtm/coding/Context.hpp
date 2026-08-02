#pragma once
#include <cstdint>
#include <functional>

namespace xtm::coding {

enum class WaveletSubband : uint8_t {
    LL = 0,
    LH = 1,
    HL = 2,
    HH = 3,
    None = 4
};

struct ContextVector {
    uint8_t predictor_type;  // e.g., Left=0, Above=1, Average=2, Gradient=3, JpegLs=4, Plane=5
    uint8_t quadtree_depth;  // e.g., 0 for max block size, incrementing for splits
    WaveletSubband subband;
    uint8_t magnitude_class; // log2(magnitude) neighborhood estimation
    
    bool operator==(const ContextVector& other) const {
        return predictor_type == other.predictor_type &&
               quadtree_depth == other.quadtree_depth &&
               subband == other.subband &&
               magnitude_class == other.magnitude_class;
    }
};

} // namespace xtm::coding

// Hash specialization so ContextVector can be used in unordered_map for probability tables
namespace std {
    template <>
    struct hash<xtm::coding::ContextVector> {
        size_t operator()(const xtm::coding::ContextVector& c) const {
            return (static_cast<size_t>(c.predictor_type) << 24) ^
                   (static_cast<size_t>(c.quadtree_depth) << 16) ^
                   (static_cast<size_t>(c.subband) << 8) ^
                   (static_cast<size_t>(c.magnitude_class));
        }
    };
}
