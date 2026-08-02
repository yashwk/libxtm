#include "xtm/analyzer/Statistics.hpp"
#include <unordered_map>
#include <cmath>

namespace xtm::analyzer {

double calculate_entropy(const std::vector<float>& data) {
    std::unordered_map<float, std::size_t> counts;
    for (float val : data) {
        counts[val]++;
    }

    double entropy = 0.0;
    double total = static_cast<double>(data.size());
    if (total == 0) return 0.0;

    for (const auto& [val, count] : counts) {
        double p = static_cast<double>(count) / total;
        entropy -= p * std::log2(p);
    }
    return entropy;
}

double calculate_entropy(const std::vector<int32_t>& data) {
    std::unordered_map<int32_t, std::size_t> counts;
    for (int32_t val : data) {
        counts[val]++;
    }

    double entropy = 0.0;
    double total = static_cast<double>(data.size());
    if (total == 0) return 0.0;

    for (const auto& [val, count] : counts) {
        double p = static_cast<double>(count) / total;
        entropy -= p * std::log2(p);
    }
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
