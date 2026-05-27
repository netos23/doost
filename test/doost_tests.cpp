#include "doost/downward_pool.hpp"
#include "doost/downward_pool_allocator.hpp"
#include "doost/list.hpp"
#include "doost/nonblocking_downward_pool.hpp"

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
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

#define CHECK_THROWS_AS(expression, exception_type)                             \
    do {                                                                       \
        bool caught_expected_exception = false;                                 \
        try {                                                                  \
            expression;                                                         \
        } catch (const exception_type&) {                                       \
            caught_expected_exception = true;                                   \
        } catch (...) {                                                         \
        }                                                                       \
        CHECK(caught_expected_exception);                                       \
    } while (false)

    template <class T, class Allocator>
    std::vector<T> values_from(const doost::List<T, Allocator>& list) {
        std::vector<T> values;
        for (const T& value : list) {
            values.push_back(value);
        }
        return values;
    }

    template <class T>
    bool is_aligned(T* pointer, std::size_t alignment) {
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        return address % alignment == 0;
    }

    std::size_t system_page_size() {
        const long value = sysconf(_SC_PAGESIZE);
        if (value <= 0) {
            throw std::runtime_error("sysconf(_SC_PAGESIZE) failed");
        }
        return static_cast<std::size_t>(value);
    }

    void test_list_push_pop_and_iteration() {
        doost::List<int> list;
        CHECK(list.empty());
        CHECK(list.size() == 0);

        list.push_front(1);
        list.push_front(2);
        list.emplace_front(3);

        CHECK(!list.empty());
        CHECK(list.size() == 3);
        CHECK(list.front() == 3);
        CHECK(values_from(list) == std::vector<int>({3, 2, 1}));

        list.pop_front();
        CHECK(list.size() == 2);
        CHECK(list.front() == 2);
        CHECK(values_from(list) == std::vector<int>({2, 1}));

        list.clear();
        CHECK(list.empty());
        CHECK(list.size() == 0);
        CHECK_THROWS_AS(list.pop_front(), std::out_of_range);
    }

    void test_list_copy_and_move() {
        doost::List<int> source;
        for (int value = 0; value != 4; ++value) {
            source.push_front(value);
        }

        const doost::List<int> copy(source);
        CHECK(source.size() == 4);
        CHECK(copy.size() == 4);
        CHECK(values_from(copy) == std::vector<int>({3, 2, 1, 0}));

        doost::List<int> assigned;
        assigned = source;
        CHECK(assigned.size() == 4);
        CHECK(values_from(assigned) == std::vector<int>({3, 2, 1, 0}));

        doost::List<int> moved(std::move(source));
        CHECK(source.empty());
        CHECK(moved.size() == 4);
        CHECK(values_from(moved) == std::vector<int>({3, 2, 1, 0}));
    }

    void test_pool_grows_down_and_resets() {
        doost::DownwardPool pool(128);

        auto* first = static_cast<std::max_align_t*>(
            pool.allocate(sizeof(std::max_align_t), alignof(std::max_align_t)));
        auto* second = static_cast<std::max_align_t*>(
            pool.allocate(sizeof(std::max_align_t), alignof(std::max_align_t)));

        const auto first_address = reinterpret_cast<std::uintptr_t>(first);
        const auto second_address = reinterpret_cast<std::uintptr_t>(second);
        CHECK(second_address < first_address);
        CHECK(is_aligned(first, alignof(std::max_align_t)));
        CHECK(is_aligned(second, alignof(std::max_align_t)));

        pool.reset();
        auto* reset = static_cast<std::max_align_t*>(
            pool.allocate(sizeof(std::max_align_t), alignof(std::max_align_t)));
        CHECK(reinterpret_cast<std::uintptr_t>(reset) == first_address);
        CHECK(is_aligned(reset, alignof(std::max_align_t)));

        pool.release();
    }

    void test_pool_move_transfers_mapping() {
        doost::DownwardPool source(64);
        auto* first = static_cast<std::max_align_t*>(
            source.allocate(sizeof(std::max_align_t), alignof(std::max_align_t)));
        const auto first_address = reinterpret_cast<std::uintptr_t>(first);

        doost::DownwardPool moved(std::move(source));
        auto* second = static_cast<std::max_align_t*>(
            moved.allocate(sizeof(std::max_align_t), alignof(std::max_align_t)));
        const auto second_address = reinterpret_cast<std::uintptr_t>(second);
        CHECK(second_address < first_address);
        CHECK(is_aligned(second, alignof(std::max_align_t)));

        doost::DownwardPool assigned(32);
        assigned = std::move(moved);
        auto* third = static_cast<std::max_align_t*>(
            assigned.allocate(sizeof(std::max_align_t), alignof(std::max_align_t)));
        const auto third_address = reinterpret_cast<std::uintptr_t>(third);
        CHECK(third_address < second_address);
        CHECK(is_aligned(third, alignof(std::max_align_t)));
    }

    void test_allocator_list_uses_pool_storage() {
        using Allocator = doost::DownwardPoolAllocator<unsigned>;
        using List = doost::List<unsigned, Allocator>;

        doost::DownwardPool pool(List::required_storage(4));
        {
            List list{Allocator(pool)};
            list.push_front(1);
            list.push_front(2);
            list.push_front(3);
            list.push_front(4);

            CHECK(list.size() == 4);
            CHECK(values_from(list) == std::vector<unsigned>({4, 3, 2, 1}));

            list.release_nodes();
            CHECK(list.empty());
        }
    }

    void test_nonblocking_pool_allocates_from_multiple_threads() {
        constexpr unsigned thread_count = 4;
        constexpr unsigned allocations_per_thread = 256;
        constexpr std::size_t allocation_size = sizeof(std::max_align_t);
        constexpr std::size_t allocation_count =
            thread_count * allocations_per_thread;

        doost::NonblockingDownwardPool pool(allocation_count * allocation_size,
                                            "test-nonblocking-pool");

        std::vector<std::uintptr_t> addresses(allocation_count);
        std::vector<std::thread> threads;
        threads.reserve(thread_count);
        for (unsigned thread = 0; thread != thread_count; ++thread) {
            threads.emplace_back([&, thread] {
                for (unsigned allocation = 0; allocation != allocations_per_thread;
                     ++allocation) {
                    void* memory =
                        pool.allocate(allocation_size, alignof(std::max_align_t));
                    auto* bytes = static_cast<unsigned char*>(memory);
                    bytes[0] = 0x5a;
                    bytes[allocation_size - 1] = 0xa5;
                    addresses[thread * allocations_per_thread + allocation] =
                        reinterpret_cast<std::uintptr_t>(memory);
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }

        std::sort(addresses.begin(), addresses.end());
        CHECK(addresses.front() != 0);
        CHECK(std::adjacent_find(addresses.begin(), addresses.end()) ==
            addresses.end());
        for (const std::uintptr_t address : addresses) {
            CHECK(address % alignof(std::max_align_t) == 0);
        }
    }

    void write_into_guard_page_in_child() {
        doost::DownwardPool pool(1);
        auto* memory = static_cast<volatile unsigned char*>(
            pool.allocate(system_page_size() + 1, alignof(unsigned char)));
        *memory = 0x7f;
        _exit(EXIT_SUCCESS);
    }

    void test_overflow_hits_guard_page() {
        const pid_t child = fork();
        if (child < 0) {
            throw std::runtime_error("fork failed");
        }

        if (child == 0) {
            write_into_guard_page_in_child();
        }

        int status = 0;
        const pid_t waited = waitpid(child, &status, 0);
        CHECK(waited == child);
        CHECK(WIFSIGNALED(status));
        if (WIFSIGNALED(status)) {
            const int signal = WTERMSIG(status);
            CHECK(signal == SIGSEGV || signal == SIGBUS);
        }
    }

#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
    constexpr int kPreviousSegvHandlerExitCode = 42;
    constexpr char kPreviousSegvHandlerMessage[] =
        "previous-sigsegv-handler\n";

    void previous_sigsegv_handler(int, siginfo_t*, void*) noexcept {
        write(STDERR_FILENO, kPreviousSegvHandlerMessage,
              sizeof(kPreviousSegvHandlerMessage) - 1);
        _exit(kPreviousSegvHandlerExitCode);
    }

    void write_into_named_guard_page_in_child() {
        doost::install_pool_overflow_signal_handler();
        doost::DownwardPool pool(1, "test-overflow-pool");
        auto* memory = static_cast<volatile unsigned char*>(
            pool.allocate(system_page_size() + 1, alignof(unsigned char)));
        *memory = 0x7f;
        _exit(EXIT_SUCCESS);
    }

    void test_overflow_handler_reports_pool_name() {
        int pipe_fds[2] = {-1, -1};
        if (pipe(pipe_fds) != 0) {
            throw std::runtime_error("pipe failed");
        }

        const pid_t child = fork();
        if (child < 0) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            throw std::runtime_error("fork failed");
        }

        if (child == 0) {
            close(pipe_fds[0]);
            dup2(pipe_fds[1], STDERR_FILENO);
            close(pipe_fds[1]);
            write_into_named_guard_page_in_child();
        }

        close(pipe_fds[1]);
        std::string output;
        char buffer[256];
        for (;;) {
            const ssize_t bytes = read(pipe_fds[0], buffer, sizeof(buffer));
            if (bytes <= 0) {
                break;
            }
            output.append(buffer, static_cast<std::size_t>(bytes));
        }
        close(pipe_fds[0]);

        int status = 0;
        const pid_t waited = waitpid(child, &status, 0);
        CHECK(waited == child);
        CHECK(WIFSIGNALED(status));
        if (WIFSIGNALED(status)) {
            const int signal = WTERMSIG(status);
            CHECK(signal == SIGSEGV || signal == SIGBUS);
        }
        CHECK(output.find("test-overflow-pool") != std::string::npos);
    }

    void write_to_unregistered_address_in_child() {
        struct sigaction action{};
        action.sa_sigaction = previous_sigsegv_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &action, nullptr);

        doost::install_pool_overflow_signal_handler();

        auto* memory = static_cast<volatile unsigned char*>(nullptr);
        *memory = 0x7f;
        _exit(EXIT_SUCCESS);
    }

    void test_overflow_handler_delegates_unregistered_faults() {
        int pipe_fds[2] = {-1, -1};
        if (pipe(pipe_fds) != 0) {
            throw std::runtime_error("pipe failed");
        }

        const pid_t child = fork();
        if (child < 0) {
            close(pipe_fds[0]);
            close(pipe_fds[1]);
            throw std::runtime_error("fork failed");
        }

        if (child == 0) {
            close(pipe_fds[0]);
            dup2(pipe_fds[1], STDERR_FILENO);
            close(pipe_fds[1]);
            write_to_unregistered_address_in_child();
        }

        close(pipe_fds[1]);
        std::string output;
        char buffer[256];
        for (;;) {
            const ssize_t bytes = read(pipe_fds[0], buffer, sizeof(buffer));
            if (bytes <= 0) {
                break;
            }
            output.append(buffer, static_cast<std::size_t>(bytes));
        }
        close(pipe_fds[0]);

        int status = 0;
        const pid_t waited = waitpid(child, &status, 0);
        CHECK(waited == child);
        CHECK(WIFEXITED(status));
        if (WIFEXITED(status)) {
            CHECK(WEXITSTATUS(status) == kPreviousSegvHandlerExitCode);
        }
        CHECK(output.find(kPreviousSegvHandlerMessage) != std::string::npos);
    }
#endif

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
    run_test("list push/pop/iteration", test_list_push_pop_and_iteration);
    run_test("list copy/move", test_list_copy_and_move);
    run_test("pool grows down and resets", test_pool_grows_down_and_resets);
    run_test("pool move transfers mapping", test_pool_move_transfers_mapping);
    run_test("allocator list uses pool storage", test_allocator_list_uses_pool_storage);
    run_test("nonblocking pool allocates from multiple threads",
             test_nonblocking_pool_allocates_from_multiple_threads);
    run_test("overflow hits guard page", test_overflow_hits_guard_page);
#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
    run_test("overflow handler reports pool name",
             test_overflow_handler_reports_pool_name);
    run_test("overflow handler delegates unregistered faults",
             test_overflow_handler_delegates_unregistered_faults);
#endif

    if (g_failures != 0) {
        std::cerr << g_failures << " test check(s) failed\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
