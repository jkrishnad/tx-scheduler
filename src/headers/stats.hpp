#pragma once
#include <string_view>
#include <vector>

// Every field is defined in nanoseconds
struct Stats {
    double count{};
    double min{};
    double p50{};
    double p99{};
    double p999{}; // 99.9th percentile
    double max{};
};

Stats compute_stats(std::vector<double> samples);
void print_stats(std::string_view label, const Stats &stats);
