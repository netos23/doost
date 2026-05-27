#ifndef DOOST_BENCHMARK_LIST_ALLOCATOR_BENCHMARK_HPP
#define DOOST_BENCHMARK_LIST_ALLOCATOR_BENCHMARK_HPP

#include "doost/list.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/resource.h>
#include <sys/time.h>
#include <thread>
#include <vector>

namespace doost::benchmark {
    constexpr std::size_t default_node_count = 10000000;
    constexpr unsigned default_thread_count = 16;

    struct Options {
        std::size_t node_count = default_node_count;
        unsigned thread_count = default_thread_count;
    };

    struct PoolStats {
        std::size_t usable_storage = 0;
        std::size_t used_storage = 0;
    };

    inline std::size_t checked_multiply(std::size_t lhs, std::size_t rhs) {
        if (rhs != 0 && lhs > std::numeric_limits<std::size_t>::max() / rhs) {
            throw std::length_error("benchmark size overflows size_t");
        }
        return lhs * rhs;
    }

    inline std::size_t parse_size(std::string_view text, const char* name,
                                  const char* program) {
        char* end = nullptr;
        errno = 0;
        const auto* begin = text.data();
        const unsigned long long value = std::strtoull(begin, &end, 10);
        if (errno != 0 || end == begin ||
            static_cast<std::size_t>(end - begin) != text.size() ||
            value > std::numeric_limits<std::size_t>::max()) {
            std::cerr << "Usage: " << program << " [node-count] [thread-count]\n";
            throw std::invalid_argument(std::string(name) +
                " must be a positive integer");
        }
        if (value == 0) {
            throw std::invalid_argument(std::string(name) +
                " must be greater than zero");
        }
        return static_cast<std::size_t>(value);
    }

    inline Options parse_options(int argc, const char* argv[]) {
        Options options;
        if (argc > 3) {
            std::cerr << "Usage: " << argv[0] << " [node-count] [thread-count]\n";
            throw std::invalid_argument("too many benchmark arguments");
        }
        if (argc >= 2) {
            options.node_count = parse_size(argv[1], "node-count", argv[0]);
        }
        if (argc >= 3) {
            const std::size_t thread_count =
                parse_size(argv[2], "thread-count", argv[0]);
            if (thread_count > std::numeric_limits<unsigned>::max()) {
                throw std::invalid_argument("thread-count is too large");
            }
            options.thread_count = static_cast<unsigned>(thread_count);
        }
        return options;
    }

    inline void get_usage(struct rusage& usage) {
        if (getrusage(RUSAGE_SELF, &usage) != 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "getrusage failed");
        }
    }

    inline std::uint64_t max_rss_bytes(const struct rusage& usage) {
#if defined(__APPLE__) && defined(__MACH__)
        return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
    }

    inline std::uint64_t time_value_us(const struct timeval& value) {
        return static_cast<std::uint64_t>(value.tv_sec) * 1000000ULL +
            static_cast<std::uint64_t>(value.tv_usec);
    }

    inline std::uint64_t cpu_time_us(const struct rusage& start,
                                     const struct rusage& finish) {
        const std::uint64_t start_us =
            time_value_us(start.ru_utime) + time_value_us(start.ru_stime);
        const std::uint64_t finish_us =
            time_value_us(finish.ru_utime) + time_value_us(finish.ru_stime);
        return finish_us >= start_us ? finish_us - start_us : 0;
    }

    template <class List>
    void fill_list(List& list, std::size_t node_count) {
        for (std::size_t i = 0; i != node_count; ++i) {
            list.push_front(static_cast<unsigned>(i));
        }
    }

    template <class Function>
    void run_threads(unsigned thread_count, Function function) {
        std::vector<std::thread> threads;
        std::vector<std::exception_ptr> exceptions(thread_count);
        threads.reserve(thread_count);

        for (unsigned index = 0; index != thread_count; ++index) {
            threads.emplace_back([&, index] {
                try {
                    function(index);
                }
                catch (...) {
                    exceptions[index] = std::current_exception();
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        for (const auto& exception : exceptions) {
            if (exception != nullptr) {
                std::rethrow_exception(exception);
            }
        }
    }

    inline void print_result(std::string_view allocator_name,
                             const Options& options, std::uint64_t wall_time_us,
                             std::uint64_t cpu_time, std::uint64_t memory_used,
                             std::size_t node_storage_required,
                             const PoolStats& pool_stats) {
        std::cout << "Allocator: " << allocator_name << '\n';
        std::cout << "Threads: " << options.thread_count << '\n';
        std::cout << "Node count per thread: " << options.node_count << '\n';
        std::cout << "Total nodes: "
            << checked_multiply(options.node_count, options.thread_count)
            << '\n';
        std::cout << "Time used: " << wall_time_us << " usec\n";
        std::cout << "CPU time used: " << cpu_time << " usec\n";
        std::cout << "Memory used: " << memory_used << " bytes\n";
        std::cout << "Node storage required: " << node_storage_required
            << " bytes\n";
        std::cout << "Pool usable storage: " << pool_stats.usable_storage
            << " bytes\n";
        std::cout << "Pool used storage: " << pool_stats.used_storage
            << " bytes\n";

        const double overhead =
            memory_used == 0
                ? 0.0
                : (static_cast<double>(memory_used) -
                    static_cast<double>(node_storage_required)) *
                100.0 / static_cast<double>(memory_used);
        std::cout << "Overhead: " << std::fixed << std::setw(4)
            << std::setprecision(1) << overhead << "%\n";
    }

    template <class Work>
    int run_benchmark(int argc, const char* argv[],
                      std::string_view allocator_name, std::size_t node_size,
                      Work work) {
        try {
            const Options options = parse_options(argc, argv);
            const std::size_t node_storage_required =
                checked_multiply(
                    checked_multiply(options.node_count, options.thread_count),
                    node_size);

            struct rusage start_usage{};
            struct rusage finish_usage{};
            get_usage(start_usage);
            const auto start = std::chrono::steady_clock::now();

            const PoolStats pool_stats = work(options, node_storage_required);

            const auto finish = std::chrono::steady_clock::now();
            get_usage(finish_usage);

            const auto wall_time_us =
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        finish - start)
                    .count());
            std::uint64_t memory_used = max_rss_bytes(finish_usage);
            const std::uint64_t direct_storage =
                static_cast<std::uint64_t>(
                    pool_stats.usable_storage > node_storage_required
                        ? pool_stats.usable_storage
                        : node_storage_required);
            if (memory_used < direct_storage) {
                memory_used = direct_storage;
            }

            print_result(allocator_name, options, wall_time_us,
                         cpu_time_us(start_usage, finish_usage), memory_used,
                         node_storage_required, pool_stats);
            return EXIT_SUCCESS;
        }
        catch (const std::exception& exception) {
            std::cerr << "Benchmark failed: " << exception.what() << '\n';
            return EXIT_FAILURE;
        }
    }
} // namespace doost::benchmark

#endif // DOOST_BENCHMARK_LIST_ALLOCATOR_BENCHMARK_HPP
