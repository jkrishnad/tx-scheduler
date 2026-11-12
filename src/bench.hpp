#pragma once
#include <chrono>
#include <ratio>
#include <vector>

template <typename F> std::vector<double> time_runs(F fn, int runs) {
    std::vector<double> times;

    times.reserve(runs);
    for (int r = 0; r < runs; r++) {

        auto t0 = std::chrono::steady_clock::now();
        fn();
        auto t1 = std::chrono::steady_clock::now();

        times.push_back(
            std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return times;
};
