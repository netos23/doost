#include "doost/parallel_memcpy.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
    constexpr std::size_t kDefaultBytes = 256ULL * 1024ULL * 1024ULL;
    constexpr std::size_t kDefaultMaxThreads = 8;
    constexpr unsigned kDefaultRepeats = 1;

    std::size_t parse_size(std::string_view value, const char* name) {
        std::size_t parsed = 0;
        try {
            parsed = static_cast<std::size_t>(std::stoull(std::string(value)));
        }
        catch (const std::exception&) {
            throw std::invalid_argument(std::string("invalid ") + name);
        }
        return parsed;
    }

    void fill_source(std::vector<std::uint8_t>& source) {
        for (std::size_t index = 0; index != source.size(); ++index) {
            source[index] = static_cast<std::uint8_t>(
                (index * 131U + index / 5U + 0x33U) & 0xffU);
        }
    }

    bool same_bytes(const std::vector<std::uint8_t>& lhs,
                    const std::vector<std::uint8_t>& rhs) {
        return lhs.size() == rhs.size() &&
            std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
    }

    double mib_per_second(std::size_t bytes, double seconds) {
        if (seconds == 0.0) {
            return 0.0;
        }
        constexpr double mib = 1024.0 * 1024.0;
        return static_cast<double>(bytes) / mib / seconds;
    }
} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t bytes =
            argc > 1 ? parse_size(argv[1], "byte count") : kDefaultBytes;
        const std::size_t max_threads =
            argc > 2
                ? parse_size(argv[2], "max thread count")
                : kDefaultMaxThreads;
        const unsigned repeats =
            argc > 3
                ? static_cast<unsigned>(parse_size(argv[3], "repeat count"))
                : kDefaultRepeats;
        if (repeats == 0) {
            throw std::invalid_argument("repeat count must be positive");
        }

        std::vector<std::uint8_t> source(bytes);
        std::vector<std::uint8_t> destination(bytes);
        fill_source(source);

        std::cout << "Bytes: " << bytes << '\n';
        std::cout << "Repeats: " << repeats << '\n';
        std::cout << "Threads, Best us, MiB/s\n";

        for (std::size_t thread_count = 0; thread_count <= max_threads;
             ++thread_count) {
            doost::set_parallel_memcpy_thread_count(thread_count);

            double best_seconds = 0.0;
            for (unsigned repeat = 0; repeat != repeats; ++repeat) {
                std::fill(destination.begin(), destination.end(),
                          static_cast<std::uint8_t>(0xa5));

                const auto started = std::chrono::steady_clock::now();
                void* result = doost::parallel_memcpy(
                    destination.data(), source.data(), source.size());
                const auto finished = std::chrono::steady_clock::now();

                if (result != destination.data() ||
                    !same_bytes(source, destination)) {
                    std::cerr << "copy verification failed for thread count "
                        << thread_count << '\n';
                    return EXIT_FAILURE;
                }

                const std::chrono::duration<double> elapsed =
                    finished - started;
                if (repeat == 0 || elapsed.count() < best_seconds) {
                    best_seconds = elapsed.count();
                }
            }

            const auto best_microseconds =
                static_cast<long long>(best_seconds * 1'000'000.0);
            std::cout << thread_count << ", " << best_microseconds << ", "
                << mib_per_second(bytes, best_seconds) << '\n';
        }
    }
    catch (const std::exception& exception) {
        std::cerr << "parallel_memcpy_benchmark: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
