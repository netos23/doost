#include "doost/string.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <sys/resource.h>
#include <sys/time.h>
#include <vector>

namespace {

constexpr unsigned default_string_count = 5000;

void get_usage(struct rusage& usage) {
    if (getrusage(RUSAGE_SELF, &usage)) {
        perror("Cannot get usage");
        std::exit(EXIT_FAILURE);
    }
}

uint64_t max_rss_bytes(const struct rusage& usage) {
#if defined(__APPLE__) && defined(__MACH__)
    return static_cast<uint64_t>(usage.ru_maxrss);
#else
    return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
}

uint64_t user_time_us(const struct rusage& start,
                      const struct rusage& finish) {
    auto seconds = finish.ru_utime.tv_sec - start.ru_utime.tv_sec;
    auto useconds = finish.ru_utime.tv_usec - start.ru_utime.tv_usec;
    if (useconds < 0) {
        --seconds;
        useconds += 1000000;
    }
    return static_cast<uint64_t>(seconds) * 1000000ULL +
           static_cast<uint64_t>(useconds);
}

unsigned parse_string_count(const int argc, const char* argv[]) {
    if (argc < 2) {
        return default_string_count;
    }

    char* end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' ||
        value > std::numeric_limits<unsigned>::max()) {
        std::cerr << "Usage: " << argv[0] << " [string-count]\n";
        std::exit(EXIT_FAILURE);
    }

    return static_cast<unsigned>(value);
}

std::string make_value(unsigned value) {
    return "string-" + std::to_string(value);
}

void bubble_sort(std::vector<doost::String>& values) {
    if (values.empty()) {
        return;
    }

    for (std::size_t pass = 0; pass != values.size(); ++pass) {
        for (std::size_t i = 1; i < values.size() - pass; ++i) {
            if (values[i] < values[i - 1]) {
                using std::swap;
                swap(values[i - 1], values[i]);
            }
        }
    }
}

std::size_t count_unique(const std::vector<doost::String>& values) {
    std::size_t result = 0;
    for (const doost::String& value : values) {
        if (value.unique()) {
            ++result;
        }
    }
    return result;
}

void run_benchmark(unsigned n) {
    struct rusage start;
    struct rusage finish;

    get_usage(start);

    std::vector<doost::String> values;
    std::vector<doost::String> aliases;
    values.reserve(n);
    aliases.reserve(n / 8 + 1);

    for (unsigned i = 0; i != n; ++i) {
        values.emplace_back(make_value(n - i));
        if (i % 8 == 0) {
            aliases.emplace_back(values.back());
        }
    }

    const std::size_t unique_before = count_unique(values);
    bubble_sort(values);
    const std::size_t unique_after = count_unique(values);

    get_usage(finish);

    const uint64_t time_used = user_time_us(start, finish);
    std::cout << "Time used: " << time_used << " usec\n";

    const uint64_t start_rss = max_rss_bytes(start);
    const uint64_t finish_rss = max_rss_bytes(finish);
    const uint64_t mem_used = finish_rss > start_rss ? finish_rss - start_rss : 0;
    std::cout << "Memory used: " << mem_used << " bytes\n";

    std::cout << "String count: " << n << '\n';
    std::cout << "External aliases: " << aliases.size() << '\n';
    std::cout << "Unique before sort: " << unique_before << '\n';
    std::cout << "Unique after sort: " << unique_after << '\n';
    std::cout << "First value after sort: "
              << (values.empty() ? "" : values.front().c_str()) << '\n';
    std::cout << "Last value after sort: "
              << (values.empty() ? "" : values.back().c_str()) << '\n';
}

} // namespace

int main(const int argc, const char* argv[]) {
    run_benchmark(parse_string_count(argc, argv));
    return EXIT_SUCCESS;
}
