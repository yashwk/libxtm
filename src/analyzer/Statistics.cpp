#include "xtm/analyzer/Statistics.hpp"
#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace xtm::analyzer {

double calculate_entropy(const std::vector<float>& data) {
    if (data.empty()) return 0.0;
    std::vector<float> sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end());

    double entropy = 0.0;
    double total = static_cast<double>(data.size());
    std::size_t current_count = 1;
    for (std::size_t i = 1; i < sorted_data.size(); ++i) {
        if (sorted_data[i] == sorted_data[i-1]) {
            current_count++;
        } else {
            double p = static_cast<double>(current_count) / total;
            entropy -= p * std::log2(p);
            current_count = 1;
        }
    }
    double p = static_cast<double>(current_count) / total;
    entropy -= p * std::log2(p);
    return entropy;
}

double calculate_entropy(const std::vector<int32_t>& data) {
    if (data.empty()) return 0.0;
    std::vector<int32_t> sorted_data = data;
    std::sort(sorted_data.begin(), sorted_data.end());

    double entropy = 0.0;
    double total = static_cast<double>(data.size());
    std::size_t current_count = 1;
    for (std::size_t i = 1; i < sorted_data.size(); ++i) {
        if (sorted_data[i] == sorted_data[i-1]) {
            current_count++;
        } else {
            double p = static_cast<double>(current_count) / total;
            entropy -= p * std::log2(p);
            current_count = 1;
        }
    }
    double p = static_cast<double>(current_count) / total;
    entropy -= p * std::log2(p);
    return entropy;
}

double calculate_entropy(const std::unordered_map<int32_t, std::size_t>& counts, std::size_t total_count) {
    double entropy = 0.0;
    double total = static_cast<double>(total_count);
    if (total == 0) return 0.0;
    for (const auto& [val, count] : counts) {
        if (count > 0) {
            double p = static_cast<double>(count) / total;
            entropy -= p * std::log2(p);
        }
    }
    return entropy;
}

} // namespace xtm::analyzer
