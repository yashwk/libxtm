#include <gtest/gtest.h>
#include "xtm/terrain/Quantization.hpp"
#include "xtm/partition/Block.hpp"
#include "xtm/analyzer/Selector.hpp"
#include "xtm/predictor/Predictors.hpp"
#include "xtm/transform/Wavelet.hpp"
#include "xtm/coding/RangeCoder.hpp"
#include "xtm/coding/ContextModeler.hpp"
#include "xtm/coding/ZigZag.hpp"
#include <random>
#include <cmath>
#include <unordered_map>

using namespace xtm;

namespace {

// Mirrors the per-block bitstream path of apps/xtm/EncodeCmd.cpp / DecodeCmd.cpp.
// The selector already stores wavelet-transformed residuals when use_wavelet is set,
// so this only runs symbol generation -> adaptive arithmetic coding -> decode.
void roundtrip_block(const std::vector<int32_t>& residuals_in,
                     bool use_wavelet, uint32_t levels,
                     uint32_t w, uint32_t h,
                     coding::ContextModel model,
                     std::vector<int32_t>& residuals_out) {
    std::vector<int32_t> data = residuals_in;
    
    coding::BitWriter bw;
    coding::ArithmeticEncoder ac(bw);
    std::unordered_map<coding::Context, coding::FrequencyTable> context_tables;
    coding::FrequencyTable run_table(256);
    coding::FrequencyTable uniform_bit(2);
    
    auto symbols = coding::generate_symbols(data, w, h, use_wavelet ? levels : 0, model);
    for (const auto& sym : symbols) {
        auto it = context_tables.find(sym.context);
        if (it == context_tables.end()) {
            it = context_tables.emplace(sym.context, coding::FrequencyTable(33)).first;
        }
        auto& freqs = it->second;
        
        ac.encode(freqs, sym.magnitude_class);
        freqs.increment(sym.magnitude_class);
        
        if (sym.magnitude_class == 0) {
            ac.encode(run_table, sym.run_length - 1);
            run_table.increment(sym.run_length - 1);
        } else if (sym.magnitude_class > 1) {
            for (int i = sym.magnitude_class - 2; i >= 0; --i) {
                ac.encode(uniform_bit, (sym.remainder >> i) & 1);
            }
        }
    }
    ac.flush();
    bw.flush();
    
    coding::BitReader br(bw.get_buffer());
    coding::ArithmeticDecoder ad(br);
    
    std::vector<std::pair<uint32_t, uint32_t>> coords_LL, coords_LH, coords_HL, coords_HH;
    coding::extract_subbands(w, h, use_wavelet ? levels : 0, coords_LL, coords_LH, coords_HL, coords_HH);
    
    std::unordered_map<coding::Context, coding::FrequencyTable> dec_context_tables;
    coding::FrequencyTable dec_run_table(256);
    coding::FrequencyTable dec_uniform_bit(2);
    
    residuals_out.assign(w * h, 0);
    
    auto decode_subband = [&](const std::vector<std::pair<uint32_t, uint32_t>>& coords, uint8_t sb_idx) {
        uint32_t decoded_count = 0;
        while (decoded_count < coords.size()) {
            coding::Context ctx;
            ctx.subband = sb_idx;
            ctx.neighbour_activity = 0;
            if (model == coding::ContextModel::Extended && decoded_count > 0) {
                uint32_t prev_x = coords[decoded_count - 1].first;
                uint32_t prev_y = coords[decoded_count - 1].second;
                if (std::abs(residuals_out[prev_y * w + prev_x]) > 2) {
                    ctx.neighbour_activity = 1;
                }
            }
            
            auto it = dec_context_tables.find(ctx);
            if (it == dec_context_tables.end()) {
                it = dec_context_tables.emplace(ctx, coding::FrequencyTable(33)).first;
            }
            auto& freqs = it->second;
            
            uint32_t mag_class = ad.decode(freqs);
            freqs.increment(mag_class);
            
            if (mag_class == 0) {
                uint32_t run_len = ad.decode(dec_run_table) + 1;
                dec_run_table.increment(run_len - 1);
                decoded_count += run_len;
            } else {
                uint32_t remainder = 0;
                if (mag_class > 1) {
                    for (int i = mag_class - 2; i >= 0; --i) {
                        remainder |= (ad.decode(dec_uniform_bit) << i);
                    }
                }
                uint32_t zz = (1u << (mag_class - 1)) | remainder;
                int32_t val = coding::zigzag_decode(zz);
                uint32_t x = coords[decoded_count].first;
                uint32_t y = coords[decoded_count].second;
                residuals_out[y * w + x] = val;
                decoded_count++;
            }
        }
    };
    
    decode_subband(coords_LL, 0);
    decode_subband(coords_LH, 1);
    decode_subband(coords_HL, 2);
    decode_subband(coords_HH, 3);
    
    if (use_wavelet && levels > 0) {
        transform::CDF53Transform::inverse_2d(residuals_out, w, h, levels);
    }
}

struct RoundTripResult {
    int total_blocks = 0;
    int wavelet_blocks = 0;
};

RoundTripResult roundtrip_grid(terrain::IntGrid& grid, coding::ContextModel model) {
    predictor::PredictorBank bank;
    std::vector<const predictor::Predictor*> predictors_list = bank.ordered();
    analyzer::PredictorSelector selector(predictors_list, 10.0, analyzer::PipelineOrder::PredictorWavelet);
    
    terrain::IntGrid decoded;
    decoded.width = grid.width;
    decoded.height = grid.height;
    decoded.data.assign(grid.data.size(), 0);
    
    const uint32_t block_size = 64;
    RoundTripResult result;
    
    for (uint32_t by = 0; by < grid.height; by += block_size) {
        for (uint32_t bx = 0; bx < grid.width; bx += block_size) {
            partition::BlockView block;
            block.grid = &grid;
            block.x_offset = bx;
            block.y_offset = by;
            block.width = std::min(block_size, grid.width - bx);
            block.height = std::min(block_size, grid.height - by);
            
            auto sel = selector.select(block);
            if (sel.best_predictor == nullptr) {
                ADD_FAILURE() << "No predictor selected for block (" << bx << ", " << by << ")";
                return result;
            }
            result.total_blocks++;
            if (sel.use_wavelet) result.wavelet_blocks++;
            
            std::vector<int32_t> residuals_out;
            roundtrip_block(sel.best_encoded.residuals,
                            sel.use_wavelet, sel.wavelet_levels,
                            block.width, block.height, model, residuals_out);
            
            partition::MutableBlockView mblock;
            mblock.grid = &decoded;
            mblock.x_offset = bx;
            mblock.y_offset = by;
            mblock.width = block.width;
            mblock.height = block.height;
            
            predictor::PredictionResult decoded_res;
            decoded_res.residuals = std::move(residuals_out);
            decoded_res.parameters = sel.best_encoded.parameters;
            sel.best_predictor->decode(decoded_res, mblock);
        }
    }
    
    for (size_t i = 0; i < grid.data.size(); ++i) {
        EXPECT_EQ(decoded.data[i], grid.data[i]) << "Mismatch at index " << i;
    }
    return result;
}

} // namespace

TEST(CodecRoundTripTest, StructuredTerrainLossless) {
    const uint32_t W = 256, H = 256;
    std::vector<float> floats(W * H);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            double v = 1000.0 + 0.5 * x + 0.3 * y
                     + 25.0 * std::sin(x * 0.05) * std::cos(y * 0.04)
                     + 10.0 * std::sin(x * 0.2 + y * 0.15)
                     + 40.0 * std::sin(x * 0.6 + y * 0.4)
                     + 15.0 * std::sin(x * 1.3) * std::cos(y * 1.1);
            if (x < 64 && y < 64) v = 500.0; // flat plateau block
            floats[y * W + x] = static_cast<float>(v);
        }
    }
    
    TerrainBuffer buffer(W, H);
    std::copy(floats.begin(), floats.end(), buffer.data());
    auto grid = terrain::quantize(buffer.view(), 1.0);
    
    auto result = roundtrip_grid(grid, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
    // Correlated residuals mean the wavelet path must be exercised at least once
    EXPECT_GT(result.wavelet_blocks, 0);
}

TEST(CodecRoundTripTest, RandomNoiseLossless) {
    const uint32_t W = 128, H = 128;
    terrain::IntGrid grid;
    grid.width = W;
    grid.height = H;
    grid.data.resize(W * H);
    
    std::mt19937 rng(123);
    std::uniform_int_distribution<int32_t> dist(-1000, 5000);
    for (auto& v : grid.data) v = dist(rng);
    
    auto result = roundtrip_grid(grid, coding::ContextModel::Simple);
    EXPECT_GT(result.total_blocks, 0);
    // Pure noise has no residual structure; the wavelet path should be rarely chosen
    EXPECT_LE(result.wavelet_blocks, result.total_blocks / 2);
}

TEST(CodecRoundTripTest, FlatGridLossless) {
    const uint32_t W = 64, H = 64;
    terrain::IntGrid grid;
    grid.width = W;
    grid.height = H;
    grid.data.assign(W * H, 4242);
    
    auto result = roundtrip_grid(grid, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
    EXPECT_EQ(result.wavelet_blocks, 0);
}

TEST(CodecRoundTripTest, OddSizedBlocksLossless) {
    // Non-power-of-two grid: exercises edge blocks that are not 64x64
    const uint32_t W = 150, H = 97;
    terrain::IntGrid grid;
    grid.width = W;
    grid.height = H;
    grid.data.resize(W * H);
    
    std::mt19937 rng(55);
    std::uniform_int_distribution<int32_t> dist(-500, 500);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            grid.data[y * W + x] = dist(rng) + x / 2;
        }
    }
    
    auto result = roundtrip_grid(grid, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
}
