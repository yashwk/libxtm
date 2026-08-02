#pragma once
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace xtm::analyzer {

double calculate_entropy(const std::vector<float>& data);
double calculate_entropy(const std::vector<int32_t>& data);
double calculate_entropy(const std::unordered_map<int32_t, std::size_t>& counts, std::size_t total_count);

}
