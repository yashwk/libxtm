#include <gtest/gtest.h>
#include "xtm/terrain/Quantization.hpp"
#include "xtm/coding/Encoder.hpp"
#include "xtm/coding/Decoder.hpp"
#include "xtm/container/IO.hpp"
#include <random>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <vector>
#include <string>
#include <filesystem>

using namespace xtm;

namespace {

std::vector<uint8_t> read_all_bytes(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return {};
    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (ifs.read((char*)buffer.data(), size)) return buffer;
    return {};
}

class CodecRoundTripTest : public ::testing::Test {
protected:
    std::string temp_dir_;

    void SetUp() override {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(10000, 99999);
        temp_dir_ = std::filesystem::temp_directory_path() / ("xtm_test_roundtrip_" + std::to_string(dis(gen)));
        std::filesystem::create_directory(temp_dir_);
    }

    void TearDown() override {
        if (std::filesystem::exists(temp_dir_)) {
            std::filesystem::remove_all(temp_dir_);
        }
    }

    std::string get_temp_file(const std::string& name) {
        return (std::filesystem::path(temp_dir_) / name).string();
    }

    coding::EncodeResult roundtrip_grid(terrain::IntGrid& grid, float precision, coding::ContextModel model, uint32_t threads = 0, analyzer::PipelineType pipeline = analyzer::PipelineType::Predictor) {
        const testing::TestInfo* const test_info = testing::UnitTest::GetInstance()->current_test_info();
        std::string test_name = test_info ? test_info->name() : "unknown";
        std::string temp_path = get_temp_file(test_name + ".xtm");
        
        coding::PipelineContext ctx(precision, model, pipeline);
        ctx.num_threads = threads;
        
        container::XtmHeader header;
        header.grid_width = grid.width;
        header.grid_height = grid.height;
        header.precision = ctx.precision;
        header.context_model = static_cast<uint8_t>(ctx.context_model);
        header.pipeline_id = (ctx.pipeline_type == analyzer::PipelineType::Wavelet) ? container::XtmHeader::PIPELINE_WAVELET : container::XtmHeader::PIPELINE_PREDICTOR;
        
        coding::EncodeResult res;
        {
            container::XtmWriter writer(temp_path, header);
            res = coding::XtmEncoder::encode(grid, writer, ctx);
        }
        
        terrain::IntGrid decoded;
        decoded.width = grid.width;
        decoded.height = grid.height;
        decoded.data.resize(grid.width * grid.height, 0);
        decoded.nodata_mask.resize(grid.width * grid.height, false);
        {
            container::XtmReader reader(temp_path);
            coding::XtmDecoder::decode(reader, decoded, 0, 0, grid.width, grid.height, ctx.num_threads);
        }
        
        EXPECT_EQ(decoded.width, grid.width);
        EXPECT_EQ(decoded.height, grid.height);
        for (size_t i = 0; i < grid.data.size(); ++i) {
            EXPECT_EQ(decoded.data[i], grid.data[i]) << "Mismatch at index " << i;
        }
        
        return res;
    }
};

} // namespace

TEST_F(CodecRoundTripTest, StructuredTerrainLossless) {
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
    
    auto result = roundtrip_grid(grid, 1.0f, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
}

TEST_F(CodecRoundTripTest, StructuredTerrainWaveletLossless) {
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
    
    auto result = roundtrip_grid(grid, 1.0f, coding::ContextModel::Extended, 0, analyzer::PipelineType::Wavelet);
    EXPECT_GT(result.total_blocks, 0);
}

TEST_F(CodecRoundTripTest, RandomNoiseLossless) {
    const uint32_t W = 128, H = 128;
    terrain::IntGrid grid;
    grid.width = W; grid.height = H; grid.data.resize(W*H);
    grid.nodata_mask.resize(W*H, false);
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int32_t> dist(-100, 100);
    
    for (auto& v : grid.data) v = dist(rng);
    
    auto result = roundtrip_grid(grid, 1.0f, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
}

TEST_F(CodecRoundTripTest, FlatGridLossless) {
    const uint32_t W = 128, H = 128;
    terrain::IntGrid grid;
    grid.width = W; grid.height = H; grid.data.resize(W*H, 42);
    grid.nodata_mask.resize(W*H, false);
    
    auto result = roundtrip_grid(grid, 1.0f, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
}

TEST_F(CodecRoundTripTest, OddSizedBlocksLossless) {
    const uint32_t W = 127, H = 191; // non powers of 2
    terrain::IntGrid grid;
    grid.width = W; grid.height = H; grid.data.resize(W*H);
    grid.nodata_mask.resize(W*H, false);
    
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            grid.data[y * W + x] = x + y;
        }
    }
    
    auto result = roundtrip_grid(grid, 1.0f, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
}

TEST_F(CodecRoundTripTest, ThreadDeterminism) {
    const uint32_t W = 256, H = 256;
    terrain::IntGrid grid; grid.width = W; grid.height = H; grid.data.resize(W*H);
    std::mt19937 rng(1337); std::uniform_int_distribution<int32_t> dist(-1000, 1000);
    for (auto& v : grid.data) v = dist(rng);
    
    coding::PipelineContext ctx(1.0f, coding::ContextModel::Extended, analyzer::PipelineType::Predictor);
    container::XtmHeader header; header.grid_width = W; header.grid_height = H; header.precision = 1.0f; header.context_model = 1;
    
    std::string path1 = get_temp_file("xtm_test_det1.xtm");
    std::string path2 = get_temp_file("xtm_test_det2.xtm");
    
    ctx.num_threads = 1;
    { container::XtmWriter writer(path1, header); coding::XtmEncoder::encode(grid, writer, ctx); }
    ctx.num_threads = 4;
    { container::XtmWriter writer(path2, header); coding::XtmEncoder::encode(grid, writer, ctx); }
    
    auto bytes1 = read_all_bytes(path1);
    auto bytes2 = read_all_bytes(path2);
    
    EXPECT_GT(bytes1.size(), 0);
    EXPECT_EQ(bytes1.size(), bytes2.size());
    EXPECT_TRUE(bytes1 == bytes2);
}

TEST_F(CodecRoundTripTest, RoiCropMatchesFullDecode) {
    const uint32_t W = 256, H = 256;
    terrain::IntGrid grid;
    grid.width = W; grid.height = H; grid.data.resize(W*H);
    std::mt19937 rng(42); std::uniform_int_distribution<int32_t> dist(-500, 500);
    for (auto& v : grid.data) v = dist(rng);
    
    std::string temp_path = get_temp_file("xtm_test_roi.xtm");
    coding::PipelineContext ctx(1.0f, coding::ContextModel::Extended, analyzer::PipelineType::Predictor);
    container::XtmHeader header; header.grid_width = W; header.grid_height = H; header.precision = 1.0f; header.context_model = 1;
    {
        container::XtmWriter writer(temp_path, header);
        coding::XtmEncoder::encode(grid, writer, ctx);
    }
    
    terrain::IntGrid full_decoded;
    full_decoded.width = W;
    full_decoded.height = H;
    full_decoded.data.resize(W * H, 0);
    full_decoded.nodata_mask.resize(W * H, false);
    {
        container::XtmReader reader(temp_path);
        coding::XtmDecoder::decode(reader, full_decoded, 0, 0, W, H, ctx.num_threads);
    }
    
    terrain::IntGrid roi_decoded;
    int rx = 64, ry = 64, rw = 128, rh = 128;
    roi_decoded.width = rw;
    roi_decoded.height = rh;
    roi_decoded.data.resize(rw * rh, 0);
    roi_decoded.nodata_mask.resize(rw * rh, false);
    {
        container::XtmReader reader(temp_path);
        coding::XtmDecoder::decode(reader, roi_decoded, rx, ry, rw, rh, ctx.num_threads);
    }
    
    EXPECT_EQ(roi_decoded.width, rw);
    EXPECT_EQ(roi_decoded.height, rh);
    for (int y = 0; y < rh; ++y) {
        for (int x = 0; x < rw; ++x) {
            EXPECT_EQ(roi_decoded.data[y * rw + x], full_decoded.data[(ry + y) * W + (rx + x)]);
        }
    }
}

TEST_F(CodecRoundTripTest, HighPrecisionSubMeterLossless) {
    const uint32_t W = 128, H = 128;
    std::vector<float> floats(W * H);
    for (uint32_t y = 0; y < H; ++y) {
        for (uint32_t x = 0; x < W; ++x) {
            double v = 1000.0 + 0.5 * x + 0.3 * y;
            // Add some precision noise
            v += (x % 7) * 0.01 + (y % 5) * 0.03;
            floats[y * W + x] = static_cast<float>(v);
        }
    }
    
    TerrainBuffer buffer(W, H);
    std::copy(floats.begin(), floats.end(), buffer.data());
    
    // Scale = 0.01 triggers the split-precision pipeline
    auto grid = terrain::quantize(buffer.view(), 0.01f);
    
    auto result = roundtrip_grid(grid, 0.01f, coding::ContextModel::Extended);
    EXPECT_GT(result.total_blocks, 0);
}

