#include <gtest/gtest.h>
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include <random>

using namespace xtm::coding;

namespace {

std::vector<int32_t> roundtrip(const std::vector<int32_t>& data, uint32_t width,
                               uint32_t height, PipelineContext& pctx) {
    BitWriter bw;
    ArithmeticEncoder ac(bw);
    EncodingContext ctx;
    encode_stream(data, width, height, pctx, ac, ctx);
    ac.flush();
    bw.flush();

    BitReader br(bw.get_buffer());
    ArithmeticDecoder ad(br);
    EncodingContext dec_ctx;
    std::vector<int32_t> decoded_data(data.size(), 0);
    decode_stream(decoded_data, width, height, pctx, ad, dec_ctx);
    return decoded_data;
}

} // namespace

TEST(ContextModelerTest, EncodeDecodeWithoutPrecision) {
    std::vector<int32_t> data = {1, 2, 3, 4, 5, 6, 7, 8};
    uint32_t width = 4;
    uint32_t height = 2;

    PipelineContext pctx(1.0, ContextModel::Simple);
    EXPECT_EQ(roundtrip(data, width, height, pctx), data);
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

    PipelineContext pctx(0.1, ContextModel::Simple);
    EXPECT_EQ(roundtrip(data, width, height, pctx), data);
}

TEST(ContextModelerTest, ExtendedRoundTrip) {
    // Extended: 2-bit activity buckets from the west/north neighbours.
    std::vector<int32_t> data = {5, 0, 0, 0, -3, 2, 0, 0, 7, 0, 0, 0, 0, 1, 0, 0};
    uint32_t width = 4;
    uint32_t height = 4;

    PipelineContext pctx(1.0, ContextModel::Extended);
    EXPECT_EQ(roundtrip(data, width, height, pctx), data);
}

TEST(ContextModelerTest, SplitPrecisionExtendedRoundTrip) {
    std::vector<int32_t> data = {
        // Meter stream
        100, 99, 0, 0, 98, 97, 96, 95,
        // Precision stream
        7, 0, 0, 3, 0, 0, 0, 2
    };
    uint32_t width = 4;
    uint32_t height = 2;

    PipelineContext pctx(0.1, ContextModel::Extended);
    EXPECT_EQ(roundtrip(data, width, height, pctx), data);
}

TEST(ContextModelerTest, IntMinValueRoundTrip) {
    // safe_abs must handle INT32_MIN without UB.
    std::vector<int32_t> data = {0, INT32_MIN, INT32_MAX, 1, -1, 0, 0, 0};
    uint32_t width = 4;
    uint32_t height = 2;

    PipelineContext pctx(1.0, ContextModel::Extended);
    EXPECT_EQ(roundtrip(data, width, height, pctx), data);
}

TEST(ContextModelerTest, TruncatedStreamClampsRuns) {
    // A truncated stream must never write out of bounds: the run decode is
    // clamped, and the caller's bitstream-underflow check flags the corruption.
    std::vector<int32_t> data(64 * 64);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int32_t> d(-500, 500);
    for (auto& v : data) v = d(rng);

    BitWriter bw;
    ArithmeticEncoder ac(bw);
    EncodingContext ctx;
    PipelineContext pctx(1.0, ContextModel::Simple);
    encode_stream(data, 64, 64, pctx, ac, ctx);
    ac.flush();
    bw.flush();

    std::vector<uint8_t> stream = bw.get_buffer();
    ASSERT_GT(stream.size(), 16u);
    std::vector<uint8_t> truncated(stream.begin(), stream.begin() + stream.size() / 4);

    BitReader br(truncated);
    ArithmeticDecoder ad(br);
    EncodingContext dec_ctx;
    std::vector<int32_t> decoded_data(64 * 64, 0);
    decode_stream(decoded_data, 64, 64, pctx, ad, dec_ctx);

    EXPECT_EQ(decoded_data.size(), 64u * 64);
    EXPECT_GT(br.excess_bits(), 0u);
}
