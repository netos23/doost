#include "doost/detail/downward_pool_storage.hpp"

#include "doost/downward_pool.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace doost::detail {
    namespace {
#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
        constexpr std::size_t kMaxRegisteredPools = 1024;

        struct PoolRegistryEntry {
            std::atomic<std::uintptr_t> guard_begin{0};
            std::atomic<std::uintptr_t> guard_end{0};
            std::atomic<const char*> name{nullptr};
        };

        std::array<PoolRegistryEntry, kMaxRegisteredPools> g_pool_registry;
        std::atomic<bool> g_pool_signal_handler_installed{false};
        struct sigaction g_previous_sigsegv{};
#if defined(SIGBUS)
        struct sigaction g_previous_sigbus{};
#endif

        void write_all(const char* text) noexcept {
            if (text == nullptr) {
                return;
            }

            const std::size_t size = std::strlen(text);
            write(STDERR_FILENO, text, size);
        }

        const char* signal_name(int signal_number) noexcept {
            switch (signal_number) {
            case SIGSEGV:
                return "SIGSEGV";
#if defined(SIGBUS)
            case SIGBUS:
                return "SIGBUS";
#endif
            default:
                return "signal";
            }
        }

        int register_pool_guard(
            std::byte* guard_begin,
            std::byte* guard_end,
            const char* name
        ) noexcept {
            for (std::size_t index = 0; index != g_pool_registry.size(); ++index) {
                if (g_pool_registry[index].guard_begin.load(std::memory_order_acquire) == 0) {
                    g_pool_registry[index].name.store(name, std::memory_order_relaxed);
                    g_pool_registry[index].guard_end.store(
                        reinterpret_cast<std::uintptr_t>(guard_end),
                        std::memory_order_relaxed
                    );
                    g_pool_registry[index].guard_begin.store(
                        reinterpret_cast<std::uintptr_t>(guard_begin),
                        std::memory_order_release
                    );
                    return static_cast<int>(index);
                }
            }

            return -1;
        }

        void unregister_pool_guard(int slot) noexcept {
            if (slot < 0 ||
                static_cast<std::size_t>(slot) >= g_pool_registry.size()) {
                return;
            }

            auto& entry = g_pool_registry[static_cast<std::size_t>(slot)];
            entry.guard_begin.store(0, std::memory_order_release);
            entry.guard_end.store(0, std::memory_order_relaxed);
            entry.name.store(nullptr, std::memory_order_relaxed);
        }

        const char* find_pool_name(void* fault_address) noexcept {
            const auto address = reinterpret_cast<std::uintptr_t>(fault_address);
            for (const auto& entry : g_pool_registry) {
                const std::uintptr_t begin = entry.guard_begin.load(std::memory_order_acquire);
                if (begin == 0) {
                    continue;
                }

                const std::uintptr_t end =
                    entry.guard_end.load(std::memory_order_relaxed);
                if (begin <= address && address < end) {
                    const char* name = entry.name.load(std::memory_order_relaxed);
                    return name == nullptr ? "unnamed-pool" : name;
                }
            }

            return nullptr;
        }

        const struct sigaction* previous_signal_action(
            int signal_number) noexcept {
            switch (signal_number) {
            case SIGSEGV:
                return &g_previous_sigsegv;
#if defined(SIGBUS)
            case SIGBUS:
                return &g_previous_sigbus;
#endif
            default:
                return nullptr;
            }
        }

        [[noreturn]] void raise_with_default_action(int signal_number) noexcept {
            struct sigaction action{};
            action.sa_handler = SIG_DFL;
            sigemptyset(&action.sa_mask);
            sigaction(signal_number, &action, nullptr);
            kill(getpid(), signal_number);
            _exit(128 + signal_number);
        }

        [[noreturn]] void call_previous_signal_handler(
            int signal_number, siginfo_t* info, void* context) noexcept {
            const struct sigaction* previous =
                previous_signal_action(signal_number);
            if (previous == nullptr || previous->sa_handler == SIG_DFL) {
                raise_with_default_action(signal_number);
            }
            if (previous->sa_handler == SIG_IGN) {
                _exit(128 + signal_number);
            }

            if ((previous->sa_flags & SA_SIGINFO) != 0) {
                previous->sa_sigaction(signal_number, info, context);
            }
            else {
                previous->sa_handler(signal_number);
            }

            _exit(128 + signal_number);
        }

        void pool_signal_handler(int signal_number, siginfo_t* info,
                                 void* context) noexcept {
            const char* pool_name =
                info == nullptr ? nullptr : find_pool_name(info->si_addr);
            if (pool_name == nullptr) {
                call_previous_signal_handler(signal_number, info, context);
            }

            write_all("doost: ");
            write_all(signal_name(signal_number));
            write_all(" while accessing protected pool \"");
            write_all(pool_name);
            write_all("\"");
            write_all("\n");

            raise_with_default_action(signal_number);
        }
#endif

#if defined(MAP_ANONYMOUS)
        constexpr int kAnonymousMap = MAP_ANONYMOUS;
#elif defined(MAP_ANON)
        constexpr int kAnonymousMap = MAP_ANON;
#else
        constexpr int kAnonymousMap = 0;
#endif

        constexpr int kPoolProtection = PROT_READ | PROT_WRITE;
        constexpr int kPoolMapFlags = MAP_PRIVATE;
        constexpr int kGuardProtection = PROT_NONE;

        void* mmap_anonymous(std::size_t bytes) {
#if defined(MAP_ANONYMOUS) || defined(MAP_ANON)
            void* mapping = mmap(nullptr, bytes, kPoolProtection,
                                 kPoolMapFlags | kAnonymousMap, -1, 0);
#else
            const int fd = open("/dev/zero", O_RDWR);
            if (fd < 0) {
                throw std::system_error(errno, std::generic_category(),
                                        "open(/dev/zero) failed");
            }
            void* mapping =
                mmap(nullptr, bytes, kPoolProtection, kPoolMapFlags, fd, 0);
            const int saved_errno = errno;
            close(fd);
            errno = saved_errno;
#endif

            return mapping;
        }

        std::size_t system_page_size() {
            errno = 0;
            const long value = sysconf(_SC_PAGESIZE);
            if (value <= 0) {
                const int error = errno == 0 ? EINVAL : errno;
                throw std::system_error(error, std::generic_category(),
                                        "sysconf(_SC_PAGESIZE) failed");
            }
            return static_cast<std::size_t>(value);
        }

        std::size_t system_page_size_noexcept() noexcept {
            const long value = sysconf(_SC_PAGESIZE);
            return value <= 0 ? 0 : static_cast<std::size_t>(value);
        }

        std::size_t round_up(std::size_t value,
                             std::size_t multiple) noexcept {
            const std::size_t mask = multiple - 1;
            return (value + mask) & ~mask;
        }
    } // namespace

    DownwardPoolStorage make_downward_pool_storage(std::size_t usable_bytes,
                                                   const char* overflow_name) {
        DownwardPoolStorage storage;
        const std::size_t guard_page_bytes = system_page_size();

        if (usable_bytes == 0) {
            usable_bytes = 1;
        }

        const std::size_t rounded_usable_bytes =
            round_up(usable_bytes, guard_page_bytes);

        if (rounded_usable_bytes >
            std::numeric_limits<std::size_t>::max() - guard_page_bytes) {
            throw std::length_error("pool mapping size overflows size_t");
        }
        storage.mapping_bytes = rounded_usable_bytes + guard_page_bytes;

        void* mapping = mmap_anonymous(storage.mapping_bytes);
        if (mapping == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "mmap failed");
        }

        storage.mapping_begin = static_cast<std::byte*>(mapping);

        if (mprotect(storage.mapping_begin, guard_page_bytes,
                     kGuardProtection) != 0) {
            const int error = errno;
            munmap(storage.mapping_begin, storage.mapping_bytes);
            throw std::system_error(error, std::generic_category(),
                                    "mprotect failed");
        }

#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
        storage.overflow_name =
            overflow_name == nullptr ? "unnamed-pool" : overflow_name;
        storage.registry_slot = register_pool_guard(
            storage.mapping_begin, storage.mapping_begin + storage.mapping_bytes,
            storage.overflow_name);
#else
        static_cast<void>(overflow_name);
#endif

        return storage;
    }

    std::size_t downward_pool_usable_bytes(
        const DownwardPoolStorage& storage) noexcept {
        if (storage.mapping_bytes == 0) {
            return 0;
        }

        const std::size_t guard_page_bytes = system_page_size_noexcept();
        if (guard_page_bytes == 0 || storage.mapping_bytes <= guard_page_bytes) {
            return 0;
        }

        return storage.mapping_bytes - guard_page_bytes;
    }

    void release_downward_pool_storage(DownwardPoolStorage& storage) noexcept {
#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
        unregister_pool_guard(storage.registry_slot);
        storage.registry_slot = -1;
        storage.overflow_name = nullptr;
#endif

        if (storage.mapping_begin != nullptr) {
            munmap(storage.mapping_begin, storage.mapping_bytes);
        }

        storage = {};
    }
} // namespace doost::detail

namespace doost {
    void install_pool_overflow_signal_handler() noexcept {
#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
        bool expected = false;
        if (!detail::g_pool_signal_handler_installed.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel,
            std::memory_order_acquire)) {
            return;
        }

        struct sigaction action{};
        action.sa_sigaction = detail::pool_signal_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
#if defined(SA_NODEFER)
        action.sa_flags |= SA_NODEFER;
#endif
        if (sigaction(SIGSEGV, &action, &detail::g_previous_sigsegv) != 0) {
            detail::g_pool_signal_handler_installed.store(
                false, std::memory_order_release);
            return;
        }
#if defined(SIGBUS)
        if (sigaction(SIGBUS, &action, &detail::g_previous_sigbus) != 0) {
            sigaction(SIGSEGV, &detail::g_previous_sigsegv, nullptr);
            detail::g_pool_signal_handler_installed.store(
                false, std::memory_order_release);
        }
#endif
#endif
    }
} // namespace doost
