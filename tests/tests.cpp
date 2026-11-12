#include <algorithm>
#include <chrono>
#include <print>
#include <random>
#include <string_view>
#include <thread>
#include <vector>

#include "bench.hpp"
#include "stats.hpp"

static int failures{};

static void check(bool ok, std::string_view name) {
    if (ok) {
        std::println("{} passed.", name);
    } else {
        ++failures;
        std::println("{} failed.", name);
    }
}

static void test_stats() {
    std::mt19937 rng{42};

    // Empty input
    {
        Stats s = compute_stats({});
        check(s.count == 0 && s.min == 0 && s.p50 == 0 && s.p99 == 0 &&
                  s.p999 == 0 && s.max == 0,
              "empty: every field is 0");
    }

    // One sample
    {
        Stats s = compute_stats({5});
        check(s.count == 1, "one: count is 1");
        check(s.min == 5 && s.p50 == 5 && s.p99 == 5 && s.p999 == 5 &&
                  s.max == 5,
              "one: every other field is 5");
    }

    // 7 samples: 10, 20, ..., 70
    {
        std::vector<double> v;
        for (int i = 1; i <= 7; ++i) {
            v.push_back(i * 10);
        }
        std::ranges::shuffle(v, rng);

        Stats s = compute_stats(v);
        check(s.count == 7, "seven: count is 7");
        check(s.min == 10, "seven: min is 10");
        check(s.max == 70, "seven: max is 70");
        check(s.p50 == 40, "seven: p50 is 40");
        check(s.p99 == 70, "seven: p99 is 70");
    }

    // 1000 samples: 1, 2, ..., 1000
    {
        std::vector<double> v;
        for (int i = 1; i <= 1000; ++i) {
            v.push_back(i);
        }
        std::ranges::shuffle(v, rng);

        Stats s = compute_stats(v);
        check(s.count == 1000, "thousand: count is 1000");
        check(s.min == 1, "thousand: min is 1");
        check(s.max == 1000, "thousand: max is 1000");
        check(s.p50 == 500, "thousand: p50 is 500");
        check(s.p99 == 990, "thousand: p99 is 990");
        check(s.p999 == 999, "thousand: p999 is 999");

        print_stats("thousand", s);
    }
}

static void test_bench() {
    // Returns one sample per run
    {
        std::vector<double> samples = time_runs([] {}, 100);
        check(samples.size() == 100, "bench: 100 runs give 100 samples");
        check(std::ranges::all_of(samples, [](double ns) { return ns >= 0; }),
              "bench: every sample is >= 0");
    }

    // Calls the function exactly `runs` times
    {
        int calls{0};
        time_runs([&calls] { ++calls; }, 50);
        check(calls == 50, "bench: function called 50 times");
    }

    // Zero runs gives no samples
    {
        std::vector<double> samples = time_runs([] {}, 0);
        check(samples.empty(), "bench: 0 runs give no samples");
    }

    // Samples are in nanoseconds: a 1 ms sleep takes at least 1,000,000 ns
    {
        std::vector<double> samples =
            time_runs([] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }, 3);
        check(std::ranges::all_of(samples, [](double ns) { return ns >= 1'000'000; }),
              "bench: 1 ms sleep measures >= 1,000,000 ns");
    }
}

int main() {
    test_stats();
    test_bench();

    std::println("\n{} failures.", failures);
    return failures == 0 ? 0 : 1;
}
