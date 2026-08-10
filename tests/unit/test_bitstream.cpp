#include <gtest/gtest.h>
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ZigZag.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include <algorithm>
#include <numeric>
#include <random>

using namespace xtm::coding;

namespace {

// Encode a synthetic residual block and return the byte stream.
std::vector<uint8_t> encode_block(std::vector<int32_t> data, uint32_t w, uint32_t h) {
    BitWriter bw;
    ArithmeticEncoder ac(bw);
    EncodingContext ctx;
    PipelineContext pctx(1.0, ContextModel::Simple);
    encode_stream(data, w, h, pctx, ac, ctx);
    ac.flush();
    bw.flush();
    return bw.get_buffer();
}

size_t excess_after_decode(const std::vector<uint8_t>& buf, uint32_t w, uint32_t h) {
    BitReader br(buf);
    ArithmeticDecoder ad(br);
    EncodingContext ctx;
    
    std::vector<int32_t> out_data(w * h, 0);
    PipelineContext pctx(1.0, ContextModel::Simple);
    decode_stream(out_data, w, h, pctx, ad, ctx);
    
    return br.excess_bits();
}

std::vector<int32_t> make_noise_block(uint32_t w, uint32_t h, int seed) {
    std::vector<int32_t> data(w * h);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int32_t> d(-500, 500);
    for (auto& v : data) v = d(rng);
    return data;
}

std::vector<int32_t> make_flat_block(uint32_t w, uint32_t h) {
    return std::vector<int32_t>(w * h, 0);
}

} // namespace

TEST(BitStreamUnderflow, ValidStreamsStayUnderTolerance) {
    constexpr uint32_t W = 64, H = 64;
    size_t max_valid_excess = 0;
    for (int seed = 0; seed < 8; ++seed) {
        auto noise = encode_block(make_noise_block(W, H, seed), W, H);
        size_t e = excess_after_decode(noise, W, H);
        max_valid_excess = std::max(max_valid_excess, e);
    }
    auto flat = encode_block(make_flat_block(W, H), W, H);
    max_valid_excess = std::max(max_valid_excess, excess_after_decode(flat, W, H));
    std::cerr << "max valid excess: " << max_valid_excess << " bits\n";
    EXPECT_LT(max_valid_excess, 512u);
}

TEST(BitStreamUnderflow, TruncatedStreamExceedsTolerance) {
    constexpr uint32_t W = 128, H = 128;
    auto stream = encode_block(make_noise_block(W, H, 7), W, H);
    EXPECT_GT(stream.size(), 16u);

    // Severe truncation (half the stream missing) is the pathological
    // truncated-but-checksummed case: the decoder falls far past EOF.
    std::vector<uint8_t> truncated(stream.begin(), stream.begin() + stream.size() / 2);
    size_t trunc_excess = excess_after_decode(truncated, W, H);
    std::cerr << "half-truncated stream: excess " << trunc_excess << " bits\n";
    EXPECT_GT(trunc_excess, 512u);
}