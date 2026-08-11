#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/PipelineContext.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include <algorithm>
#include <utility>

namespace xtm::coding {

namespace {

std::array<FrequencyTable, 8> make_mag_tables() {
    // Magnitude-class priors (33 symbols), skewed toward the small classes
    // that dominate terrain residuals, which matters for 64x64 blocks where
    // adaptation never fully converges.
    std::vector<uint32_t> prior(33, 1);
    prior[0] = 16;
    prior[1] = 8;
    prior[2] = 4;
    prior[3] = 2;
    return {FrequencyTable(prior), FrequencyTable(prior), FrequencyTable(prior),
            FrequencyTable(prior), FrequencyTable(prior), FrequencyTable(prior),
            FrequencyTable(prior), FrequencyTable(prior)};
}

FrequencyTable make_run_table() {
    // Zero-run symbols (run length - 1); runs are capped at 255, so the table
    // has 255 symbols. Short runs dominate terrain streams, so the priors
    // skew toward them.
    std::vector<uint32_t> prior(255, 1);
    prior[0] = 8;
    prior[1] = 4;
    prior[2] = 2;
    return FrequencyTable(prior);
}

} // namespace

EncodingContext::EncodingContext()
    : tables(make_mag_tables()),
      run_table(make_run_table()),
      uniform_bit(2) {}

void EncodingContext::reset() {
    for (auto& t : tables) t.reset();
    run_table.reset();
    uniform_bit.reset();
}

void encode_stream(const std::vector<int32_t>& data, uint32_t width, uint32_t height,
                   const PipelineContext& pctx, ArithmeticEncoder& ac, EncodingContext& ctx_data) {
    walk_symbols(data, width, height, pctx,
        [&](uint8_t ctx, uint8_t mag, uint32_t run, uint32_t remainder) {
            if (run > 0) {
                auto& table = ctx_data.tables[ctx];
                ac.encode(table, 0);
                table.increment(0);

                ac.encode(ctx_data.run_table, run - 1);
                ctx_data.run_table.increment(run - 1);
            } else {
                auto& table = ctx_data.tables[ctx];
                ac.encode(table, mag);
                table.increment(mag);

                if (mag > 1) {
                    for (int j = static_cast<int>(mag) - 2; j >= 0; --j) {
                        uint32_t bit = (remainder >> j) & 1;
                        ac.encode(ctx_data.uniform_bit, bit);
                    }
                }
            }
        });
}

void decode_stream(std::vector<int32_t>& data, uint32_t width, uint32_t height,
                   const PipelineContext& pctx, ArithmeticDecoder& ad, EncodingContext& ctx_data) {
    const uint32_t length = width * height;
    auto process_stream = [&](uint32_t start_idx, uint8_t stream) {
        uint32_t decoded = 0;
        while (decoded < length) {
            uint8_t ctx = context_index(
                stream, stream_activity(pctx.context_model,
                                        data, start_idx, decoded, width));

            auto& table = ctx_data.tables[ctx];
            uint32_t mag = ad.decode(table);
            table.increment(mag);

            if (mag == 0) {
                uint32_t run = ad.decode(ctx_data.run_table) + 1;
                ctx_data.run_table.increment(run - 1);

                // A run longer than the remaining samples can only come from a
                // corrupt stream (the encoder never emits one). Clamp it so the
                // write stays in bounds and let the caller's bitstream-underflow
                // check flag the corruption.
                uint32_t n = std::min(run, length - decoded);
                for (uint32_t i = 0; i < n; ++i) {
                    data[start_idx + decoded++] = 0;
                }
            } else {
                uint32_t remainder = 0;
                if (mag > 1) {
                    for (int j = static_cast<int>(mag) - 2; j >= 0; --j) {
                        remainder |= (ad.decode(ctx_data.uniform_bit) << j);
                    }
                }
                uint32_t zz = (1u << (mag - 1)) | remainder;
                data[start_idx + decoded++] = zigzag_decode(zz);
            }
        }
    };

    process_stream(0, 0);

    if (pctx.has_precision) {
        process_stream(length, 1);
    }
}

} // namespace xtm::coding
