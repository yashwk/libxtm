#include <gtest/gtest.h>
#include "xtm/analyzer/Statistics.hpp"
#include <vector>

TEST(StatisticsTest, EntropyConstantFloat) {
    std::vector<float> data(100, 42.0f);
    double entropy = xtm::analyzer::calculate_entropy(data);
    EXPECT_NEAR(entropy, 0.0, 1e-6);
}

TEST(StatisticsTest, EntropyUniformFloat) {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    double entropy = xtm::analyzer::calculate_entropy(data);
    EXPECT_NEAR(entropy, 2.0, 1e-6);
}

TEST(StatisticsTest, EntropyConstantInt) {
    std::vector<int32_t> data(100, 42);
    double entropy = xtm::analyzer::calculate_entropy(data);
    EXPECT_NEAR(entropy, 0.0, 1e-6);
}

TEST(StatisticsTest, EntropyUniformInt) {
    std::vector<int32_t> data = {1, 2, 3, 4};
    double entropy = xtm::analyzer::calculate_entropy(data);
    EXPECT_NEAR(entropy, 2.0, 1e-6);
}
