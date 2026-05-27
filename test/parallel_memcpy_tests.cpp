#include "doost/parallel_memcpy.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
    int g_failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                       \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                           \
                      << ": check failed: " #condition "\n";                  \
            ++g_failures;                                                       \
        }                                                                       \
    } while (false)

    std::vector<std::uint8_t> make_source(std::size_t size) {
        std::vector<std::uint8_t> source(size);
        for (std::size_t index = 0; index != source.size(); ++index) {
            source[index] = static_cast<std::uint8_t>(
                (index * 131U + index / 7U + 17U) & 0xffU);
        }
        return source;
    }

    bool equal_bytes(const std::vector<std::uint8_t>& lhs,
                     const std::vector<std::uint8_t>& rhs) {
        return lhs.size() == rhs.size() &&
            std::memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
    }

    void check_copy_with_default_pool(std::size_t thread_count,
                                      std::size_t size) {
        doost::set_parallel_memcpy_thread_count(thread_count);
        CHECK(doost::parallel_memcpy_thread_count() == thread_count);

        const std::vector<std::uint8_t> source = make_source(size);
        std::vector<std::uint8_t> destination(size, 0xa5);

        void* result =
            doost::parallel_memcpy(destination.data(), source.data(), size);

        CHECK(result == destination.data());
        CHECK(equal_bytes(source, destination));
    }

    void test_default_pool_copies_with_zero_to_eight_workers() {
        constexpr std::size_t one_mebibyte = 1024 * 1024;
        const std::size_t sizes[] = {
            0,
            1,
            7,
            64,
            4096,
            one_mebibyte + 123,
        };

        for (std::size_t thread_count = 0; thread_count <= 8; ++thread_count) {
            for (std::size_t size : sizes) {
                check_copy_with_default_pool(thread_count, size);
            }
        }
    }

    void test_explicit_pool_copies_repeatedly() {
        doost::ParallelMemcpyPool pool(4);
        CHECK(pool.thread_count() == 4);

        const std::vector<std::uint8_t> source = make_source(2 * 1024 * 1024 + 3);
        std::vector<std::uint8_t> destination(source.size());

        for (unsigned pass = 0; pass != 5; ++pass) {
            std::fill(destination.begin(), destination.end(),
                      static_cast<std::uint8_t>(0x10 + pass));
            void* result = pool.copy(destination.data(), source.data(),
                                     source.size());
            CHECK(result == destination.data());
            CHECK(equal_bytes(source, destination));
        }
    }

    void test_pool_can_be_resized() {
        doost::ParallelMemcpyPool pool(1);
        CHECK(pool.thread_count() == 1);

        pool.set_thread_count(6);
        CHECK(pool.thread_count() == 6);

        const std::vector<std::uint8_t> source = make_source(512 * 1024 + 11);
        std::vector<std::uint8_t> destination(source.size(), 0);
        static_cast<void>(
            pool.copy(destination.data(), source.data(), source.size()));
        CHECK(equal_bytes(source, destination));

        pool.set_thread_count(0);
        CHECK(pool.thread_count() == 0);

        std::fill(destination.begin(), destination.end(), 0);
        static_cast<void>(
            pool.copy(destination.data(), source.data(), source.size()));
        CHECK(equal_bytes(source, destination));
    }

    void test_zero_size_accepts_null_pointers() {
        doost::set_parallel_memcpy_thread_count(3);
        void* result = doost::parallel_memcpy(nullptr, nullptr, 0);
        CHECK(result == nullptr);
    }

    void run_test(std::string_view name, void (*test)()) {
        const int failures_before = g_failures;
        try {
            test();
        }
        catch (const std::exception& exception) {
            std::cerr << name << ": unexpected exception: " << exception.what()
                << '\n';
            ++g_failures;
        }
        catch (...) {
            std::cerr << name << ": unexpected non-standard exception\n";
            ++g_failures;
        }

        if (g_failures == failures_before) {
            std::cout << "[PASS] " << name << '\n';
        }
        else {
            std::cout << "[FAIL] " << name << '\n';
        }
    }
} // namespace

int main() {
    run_test("default pool copies with zero to eight workers",
             test_default_pool_copies_with_zero_to_eight_workers);
    run_test("explicit pool copies repeatedly", test_explicit_pool_copies_repeatedly);
    run_test("pool can be resized", test_pool_can_be_resized);
    run_test("zero size accepts null pointers", test_zero_size_accepts_null_pointers);

    if (g_failures != 0) {
        std::cerr << g_failures << " test check(s) failed\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
