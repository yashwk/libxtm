#include <gtest/gtest.h>
#include "xtm/terrain/Quantization.hpp"

using namespace xtm;

namespace {

TerrainBuffer make_buffer(uint32_t w, uint32_t h, const std::vector<float>& vals, std::optional<float> nodata = std::nullopt) {
    TerrainBuffer buffer(w, h);
    buffer.nodata_value = nodata;
    std::copy(vals.begin(), vals.end(), buffer.data());
    return buffer;
}

} // namespace

TEST(QuantizationTest, RoundTripScaleOne) {
    TerrainBuffer buffer = make_buffer(4, 4, {
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    });
    
    auto grid = terrain::quantize(buffer.view(), 1.0);
    ASSERT_EQ(grid.data.size(), 16u);
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(grid.data[i], static_cast<int32_t>(i + 1));
        EXPECT_FALSE(grid.nodata_mask[i]);
    }
    
    auto out = terrain::dequantize(grid, 1.0, std::nullopt, {});
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(out.data()[i], static_cast<float>(i + 1));
    }
}

TEST(QuantizationTest, RoundTripScaleHundredth) {
    // q(z) = round(z / 0.01) = round(z * 100)
    TerrainBuffer buffer = make_buffer(2, 2, {4231.37f, -12.5f, 8848.86f, 0.04f});
    
    auto grid = terrain::quantize(buffer.view(), 0.01);
    EXPECT_EQ(grid.data[0], 423137);
    EXPECT_EQ(grid.data[1], -1250);
    EXPECT_EQ(grid.data[2], 884886);
    EXPECT_EQ(grid.data[3], 4);
    
    auto out = terrain::dequantize(grid, 0.01, std::nullopt, {});
    EXPECT_FLOAT_EQ(out.data()[0], 4231.37f);
    EXPECT_FLOAT_EQ(out.data()[1], -12.5f);
    EXPECT_FLOAT_EQ(out.data()[2], 8848.86f);
    EXPECT_FLOAT_EQ(out.data()[3], 0.04f);
}

TEST(QuantizationTest, NoDataIsMaskedAndInpainted) {
    // Center pixel is NoData; quantize must mask it and fill it with the
    // average of its 4-way neighbors so predictors don't see cliffs.
    TerrainBuffer buffer = make_buffer(3, 3, {
        10.0f, 20.0f, 30.0f,
        40.0f, -9999.0f, 60.0f,
        70.0f, 80.0f, 90.0f
    }, -9999.0f);
    
    auto grid = terrain::quantize(buffer.view(), 1.0);
    ASSERT_EQ(grid.data.size(), 9u);
    
    EXPECT_TRUE(grid.nodata_mask[4]);
    EXPECT_EQ(grid.data[4], (20 + 40 + 60 + 80) / 4); // inpainting average
    
    for (size_t i = 0; i < 9; ++i) {
        if (i != 4) {
            EXPECT_FALSE(grid.nodata_mask[i]);
        }
    }
    
    // Dequantize restores the NoData value at masked pixels
    auto out = terrain::dequantize(grid, 1.0, -9999.0f, {});
    EXPECT_FLOAT_EQ(out.data()[4], -9999.0f);
    EXPECT_FLOAT_EQ(out.data()[0], 10.0f);
    EXPECT_FLOAT_EQ(out.data()[8], 90.0f);
}

TEST(QuantizationTest, NoDataWithoutValueIsNotMasked) {
    // No nodata_value means every sample is treated as valid
    TerrainBuffer buffer = make_buffer(2, 2, {1.0f, 2.0f, 3.0f, 4.0f});
    auto grid = terrain::quantize(buffer.view(), 1.0);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_FALSE(grid.nodata_mask[i]);
    }
}

TEST(QuantizationTest, EmptyGrid) {
    TerrainBuffer buffer = make_buffer(0, 0, {});
    auto grid = terrain::quantize(buffer.view(), 1.0);
    EXPECT_EQ(grid.data.size(), 0u);
    auto out = terrain::dequantize(grid, 1.0, std::nullopt, {});
    EXPECT_EQ(out.data(), nullptr);
}
