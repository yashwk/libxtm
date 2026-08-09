#include <gtest/gtest.h>
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/RangeCoder.hpp"

using namespace xtm::coding;

TEST(ContextModelerTest, EncodeDecodeWithoutPrecision) {
    std::vector<int32_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t width = 4;
    uint32_t height = 2;

    BitWriter bw;
    ArithmeticEncoder ac(bw);
    EncodingContext ctx;
    encode_stream(data, width, height, ContextModel::Simple, false, ac, ctx);
    ac.flush();
    bw.flush();

    std::vector<uint8_t> compressed = bw.get_buffer();
    EXPECT_GT(compressed.size(), 0);

    BitReader br(compressed);
    ArithmeticDecoder ad(br);
    EncodingContext dec_ctx;
    
    std::vector<int32_t> decoded_data(width * height, 0);
    decode_stream(decoded_data, width, height, ContextModel::Simple, false, ad, dec_ctx);
    
    EXPECT_EQ(data, decoded_data);
}

TEST(ContextModelerTest, EncodeDecodeWithPrecision) {
    std::vector<int32_t> data = {
        // Meter stream
        1, 2, 3, 4,
        // Precision stream
        5, 6, 7, 8
    };
    uint32_t width = 2;
    uint32_t height = 2;

    BitWriter bw;
    ArithmeticEncoder ac(bw);
    EncodingContext ctx;
    encode_stream(data, width, height, ContextModel::Simple, true, ac, ctx);
    ac.flush();
    bw.flush();

    std::vector<uint8_t> compressed = bw.get_buffer();
    EXPECT_GT(compressed.size(), 0);

    BitReader br(compressed);
    ArithmeticDecoder ad(br);
    EncodingContext dec_ctx;
    
    std::vector<int32_t> decoded_data(width * height * 2, 0);
    decode_stream(decoded_data, width, height, ContextModel::Simple, true, ad, dec_ctx);
    
    EXPECT_EQ(data, decoded_data);
}
