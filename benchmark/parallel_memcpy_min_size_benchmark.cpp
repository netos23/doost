#include "doost/parallel_memcpy.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
    constexpr std::size_t kDefaultMinBytes = 64ULL * 1024ULL;
    constexpr std::size_t kDefaultMaxBytes = 256ULL * 1024ULL * 1024ULL;
    constexpr unsigned kDefaultRepeats = 5;

    struct Options {
        std::size_t thread_count;
        std::size_t min_bytes = kDefaultMinBytes;
        std::size_t max_bytes = kDefaultMaxBytes;
        unsigned repeats = kDefaultRepeats;
    };

    std::size_t default_thread_count() noexcept {
        const unsigned hardware_threads = std::thread::hardware_concurrency();
        if (hardware_threads <= 1) {
            return 1;
        }
        return static_cast<std::size_t>(hardware_threads - 1);
    }

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

    Options parse_options(int argc, char** argv) {
        Options options{default_thread_count()};

        if (argc > 1) {
            options.thread_count = parse_size(argv[1], "thread count");
        }
        if (argc > 2) {
            options.min_bytes = parse_size(argv[2], "minimum byte count");
        }
        if (argc > 3) {
            options.max_bytes = parse_size(argv[3], "maximum byte count");
        }
        if (argc > 4) {
            options.repeats = static_cast<unsigned>(
                parse_size(argv[4], "repeat count"));
        }
        if (argc > 5) {
            throw std::invalid_argument(
                "usage: parallel_memcpy_min_size_benchmark "
                "[threads] [min-bytes] [max-bytes] [repeats]");
        }
        if (options.thread_count == 0) {
            throw std::invalid_argument("thread count must be positive");
        }
        if (options.min_bytes == 0) {
            throw std::invalid_argument("minimum byte count must be positive");
        }
        if (options.min_bytes > options.max_bytes) {
            throw std::invalid_argument(
                "minimum byte count must not exceed maximum byte count");
        }
        if (options.repeats == 0) {
            throw std::invalid_argument("repeat count must be positive");
        }

        return options;
    }

    void fill_source(std::vector<std::uint8_t>& source) {
        for (std::size_t index = 0; index != source.size(); ++index) {
            source[index] = static_cast<std::uint8_t>(
                (index * 131U + index / 7U + 0x51U) & 0xffU);
        }
    }

    bool same_bytes(const std::vector<std::uint8_t>& source,
                    const std::vector<std::uint8_t>& destination,
                    std::size_t size) {
        return std::memcmp(source.data(), destination.data(), size) == 0;
    }

    template <class Copy>
    double best_seconds(const std::vector<std::uint8_t>& source,
                        std::vector<std::uint8_t>& destination,
                        std::size_t size, unsigned repeats, Copy copy) {
        double best = 0.0;
        for (unsigned repeat = 0; repeat != repeats; ++repeat) {
            std::fill(destination.begin(), destination.begin() + size,
                      static_cast<std::uint8_t>(0xa5));

            const auto started = std::chrono::steady_clock::now();
            void* result = copy(destination.data(), source.data(), size);
            const auto finished = std::chrono::steady_clock::now();

            if (result != destination.data() ||
                !same_bytes(source, destination, size)) {
                throw std::runtime_error("copy verification failed");
            }

            const std::chrono::duration<double> elapsed = finished - started;
            if (repeat == 0 || elapsed.count() < best) {
                best = elapsed.count();
            }
        }
        return best;
    }

    long long microseconds(double seconds) {
        return static_cast<long long>(seconds * 1'000'000.0);
    }

    double speedup(double baseline_seconds, double candidate_seconds) {
        if (candidate_seconds == 0.0) {
            return 0.0;
        }
        return baseline_seconds / candidate_seconds;
    }
} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);

        std::vector<std::uint8_t> source(options.max_bytes);
        std::vector<std::uint8_t> destination(options.max_bytes);
        fill_source(source);

        doost::ParallelMemcpyPool pool(options.thread_count);

        std::size_t first_winning_size = 0;

        std::cout << "Threads: " << options.thread_count << '\n';
        std::cout << "Repeats: " << options.repeats << '\n';
        std::cout << "Configured pool threshold: "
            << doost::parallel_memcpy_min_parallel_bytes << " bytes\n";
        std::cout << "Size bytes, std memcpy best us, pool copy best us, "
            << "speedup, pool eligible\n";

        for (std::size_t size = options.min_bytes; size <= options.max_bytes;) {
            const double std_seconds =
                best_seconds(source, destination, size, options.repeats,
                             [](void* dst, const void* src, std::size_t bytes) {
                                 return std::memcpy(dst, src, bytes);
                             });

            const double parallel_seconds =
                best_seconds(source, destination, size, options.repeats,
                             [&pool](void* dst, const void* src,
                                     std::size_t bytes) {
                                 return pool.copy(dst, src, bytes);
                             });

            const double ratio = speedup(std_seconds, parallel_seconds);
            const bool pool_eligible =
                size > doost::parallel_memcpy_min_parallel_bytes;
            std::cout << size << ", " << microseconds(std_seconds) << ", "
                << microseconds(parallel_seconds) << ", " << ratio << ", "
                << (pool_eligible ? "yes" : "no") << '\n';

            if (pool_eligible && first_winning_size == 0 &&
                parallel_seconds < std_seconds) {
                first_winning_size = size;
            }

            if (size > std::numeric_limits<std::size_t>::max() / 2 ||
                size * 2 > options.max_bytes) {
                break;
            }
            size *= 2;
        }

        if (first_winning_size == 0) {
            std::cout << "Minimum winning sampled pool size: not found\n";
        }
        else {
            std::cout << "Minimum winning sampled pool size: "
                << first_winning_size << " bytes\n";
        }
    }
    catch (const std::exception& exception) {
        std::cerr << "parallel_memcpy_min_size_benchmark: "
            << exception.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
