#include <gtest/gtest.h>
#include "xtm/transform/Wavelet.hpp"
#include <random>

using namespace xtm::transform;

TEST(WaveletTest, Reversibility1D) {
    std::vector<int32_t> original = {10, 25, -5, 100, 3, 0, -42, 18, 9};
    std::vector<int32_t> data = original;
    
    CDF53Transform::forward_2d(data, original.size(), 1, 1);
    
    // Should be different
    bool different = false;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] != original[i]) different = true;
    }
    EXPECT_TRUE(different);
    
    CDF53Transform::inverse_2d(data, original.size(), 1, 1);
    
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(data[i], original[i]) << "Mismatch at index " << i;
    }
}

TEST(WaveletTest, Reversibility2D) {
    uint32_t w = 64;
    uint32_t h = 64;
    std::vector<int32_t> original(w * h);
    
    std::mt19937 rng(42);
    std::uniform_int_distribution<int32_t> dist(-1000, 1000);
    for (auto& val : original) {
        val = dist(rng);
    }
    
    std::vector<int32_t> data = original;
    CDF53Transform::forward_2d(data, w, h, 3); // 3 levels
    CDF53Transform::inverse_2d(data, w, h, 3);
    
    for (size_t i = 0; i < data.size(); ++i) {
        EXPECT_EQ(data[i], original[i]) << "Mismatch at index " << i;
    }
}
