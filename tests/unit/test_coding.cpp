#include <gtest/gtest.h>
#include "xtm/coding/ZigZag.hpp"
#include <limits>

using namespace xtm::coding;

TEST(CodingTest, ZigZagValues) {
    EXPECT_EQ(zigzag_encode(0), 0u);
    EXPECT_EQ(zigzag_encode(-1), 1u);
    EXPECT_EQ(zigzag_encode(1), 2u);
    EXPECT_EQ(zigzag_encode(-2), 3u);
    EXPECT_EQ(zigzag_encode(2), 4u);
}

TEST(CodingTest, ZigZagRoundtripSmall) {
    for (int32_t i = -1000; i <= 1000; ++i) {
        uint32_t encoded = zigzag_encode(i);
        int32_t decoded = zigzag_decode(encoded);
        EXPECT_EQ(i, decoded) << "Failed roundtrip for " << i;
    }
}

TEST(CodingTest, ZigZagRoundtripExtremes) {
    int32_t max_val = std::numeric_limits<int32_t>::max();
    int32_t min_val = std::numeric_limits<int32_t>::min();
    
    EXPECT_EQ(zigzag_decode(zigzag_encode(max_val)), max_val);
    EXPECT_EQ(zigzag_decode(zigzag_encode(min_val)), min_val);
}
