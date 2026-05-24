#include "doost/safe_memory.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>

#include <sys/mman.h>
#include <unistd.h>

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

#if defined(MAP_ANONYMOUS)
    constexpr int kAnonymousMap = MAP_ANONYMOUS;
#elif defined(MAP_ANON)
    constexpr int kAnonymousMap = MAP_ANON;
#else
#error "Anonymous mmap is required for safe memory tests"
#endif

    std::size_t page_size() {
        errno = 0;
        const long value = sysconf(_SC_PAGESIZE);
        if (value <= 0) {
            const int error = errno == 0 ? EINVAL : errno;
            throw std::system_error(error, std::generic_category(),
                                    "sysconf(_SC_PAGESIZE) failed");
        }
        return static_cast<std::size_t>(value);
    }

    class Mapping {
    public:
        explicit Mapping(std::size_t bytes, int protection = PROT_READ | PROT_WRITE)
            : bytes_(bytes) {
            void* mapping = mmap(nullptr, bytes_, protection,
                                 MAP_PRIVATE | kAnonymousMap, -1, 0);
            if (mapping == MAP_FAILED) {
                throw std::system_error(errno, std::generic_category(),
                                        "mmap failed");
            }
            data_ = static_cast<std::uint8_t*>(mapping);
        }

        ~Mapping() {
            unmap();
        }

        Mapping(const Mapping&) = delete;
        Mapping& operator=(const Mapping&) = delete;

        [[nodiscard]] std::uint8_t* data() const noexcept {
            return data_;
        }

        [[nodiscard]] std::size_t bytes() const noexcept {
            return bytes_;
        }

        void protect(int protection) const {
            if (mprotect(data_, bytes_, protection) != 0) {
                throw std::system_error(errno, std::generic_category(),
                                        "mprotect failed");
            }
        }

        void unmap() noexcept {
            if (data_ != nullptr) {
                munmap(data_, bytes_);
                data_ = nullptr;
                bytes_ = 0;
            }
        }

    private:
        std::uint8_t* data_ = nullptr;
        std::size_t bytes_ = 0;
    };

    void test_reads_plain_object() {
        const std::uint8_t value = 0xab;
        const std::optional<std::uint8_t> result = doost::safe_read_uint8(&value);
        CHECK(result.has_value());
        CHECK(result == value);
    }

    void test_returns_empty_for_null() {
        const std::optional<std::uint8_t> result =
            doost::safe_read_uint8(nullptr);
        CHECK(!result.has_value());
    }

    void test_reads_read_only_mapping() {
        Mapping mapping(page_size());
        mapping.data()[0] = 0x42;
        mapping.protect(PROT_READ);

        const std::optional<std::uint8_t> result =
            doost::safe_read_uint8(mapping.data());
        CHECK(result.has_value());
        CHECK(result == 0x42);
    }

    void test_returns_empty_for_protected_mapping() {
        Mapping mapping(page_size());
        mapping.data()[0] = 0x33;
        mapping.protect(PROT_NONE);

        const std::optional<std::uint8_t> result =
            doost::safe_read_uint8(mapping.data());
        CHECK(!result.has_value());
    }

    void test_returns_empty_for_unmapped_address() {
        Mapping mapping(page_size());
        const std::uint8_t* address = mapping.data();
        mapping.unmap();

        const std::optional<std::uint8_t> result =
            doost::safe_read_uint8(address);
        CHECK(!result.has_value());
    }

    void test_recovers_after_fault() {
        const std::optional<std::uint8_t> failed =
            doost::safe_read_uint8(nullptr);
        CHECK(!failed.has_value());

        const std::uint8_t value = 0x5a;
        const std::optional<std::uint8_t> recovered =
            doost::safe_read_uint8(&value);
        CHECK(recovered.has_value());
        CHECK(recovered == value);
    }

    void test_preserves_errno() {
        errno = ERANGE;
        static_cast<void>(doost::safe_read_uint8(nullptr));
        CHECK(errno == ERANGE);
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
    run_test("reads plain object", test_reads_plain_object);
    run_test("returns empty for null", test_returns_empty_for_null);
    run_test("reads read-only mapping", test_reads_read_only_mapping);
    run_test("returns empty for protected mapping",
             test_returns_empty_for_protected_mapping);
    run_test("returns empty for unmapped address",
             test_returns_empty_for_unmapped_address);
    run_test("recovers after fault", test_recovers_after_fault);
    run_test("preserves errno", test_preserves_errno);

    if (g_failures != 0) {
        std::cerr << g_failures << " test check(s) failed\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
