#include <gtest/gtest.h>
#include "xtm/coding/RangeCoder.hpp"
#include <random>

using namespace xtm::coding;

TEST(EntropyTest, ArithmeticEndToEnd) {
    std::vector<uint32_t> original;
    std::mt19937 rng(42);
    
    // Generate a highly skewed set of symbols (e.g. many small numbers, some large)
    for (int i = 0; i < 100000; ++i) {
        if (rng() % 100 < 80) {
            original.push_back(rng() % 5); // 80% chance of 0-4
        } else {
            original.push_back(rng() % 100000); // 20% chance of large number
        }
    }
    
    BitWriter bw;
    ArithmeticEncoder ac(bw);
    FrequencyTable enc_freqs(33); // Mag classes 0 to 32
    
    for (uint32_t val : original) {
        encode_value(ac, enc_freqs, val);
    }
    ac.flush();
    
    const auto& buffer = bw.get_buffer();
    
    // Compression Ratio check (just a sanity check that it's smaller than raw 32-bit array)
    EXPECT_LT(buffer.size(), original.size() * 4);
    
    BitReader br(buffer);
    ArithmeticDecoder ad(br);
    FrequencyTable dec_freqs(33); // Must start exactly the same
    
    for (size_t i = 0; i < original.size(); ++i) {
        uint32_t decoded = decode_value(ad, dec_freqs);
        ASSERT_EQ(decoded, original[i]) << "Failed at index " << i;
    }
}
