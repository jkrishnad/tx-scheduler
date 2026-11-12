#include "stats.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <print>
#include <vector>

static double percentile(const std::vector<double> &sorted, double p) {
    size_t n = sorted.size();

    size_t rank = std::ceil(p * n / 100);

    if (rank == 0) {
        return sorted[0];
    } else {
        return sorted[rank - 1];
    }
}

Stats compute_stats(std::vector<double> samples) {
    if (samples.empty()) {
        return Stats{};
    }

    double count = samples.size();

    // sort the vector od samples in order
    std::sort(samples.begin(), samples.end());

    double p50 = percentile(samples, 50);
    double p99 = percentile(samples, 99);
    double p999 = percentile(samples, 99.9);
    double min = samples.front();
    double max = samples.back();

    return Stats{
        .count = count,
        .min = min,
        .p50 = p50,
        .p99 = p99,
        .p999 = p999,
        .max = max,

    };
}

void print_stats(std::string_view label, const Stats &stats) {

    std::print("{} ", label);

    std::print("count={:.0f} ", stats.count);
    std::print("min={:.0f} ", stats.min);
    std::print("p50={:.0f} ", stats.p50);
    std::print("p99={:.0f} ", stats.p99);
    std::print("p99.9={:.0f} ", stats.p999);
    std::print("max={:.0f} \n", stats.max);
}
