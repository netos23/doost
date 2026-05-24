#include "doost/detail/downward_pool_storage.hpp"

#include "doost/downward_pool.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
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
        std::mutex g_pool_registry_mutex;

        void write_all(const char* text) noexcept {
            if (text == nullptr) {
                return;
            }

            std::size_t size = 0;
            while (text[size] != '\0') {
                ++size;
            }

            while (size != 0) {
                const ssize_t written = write(STDERR_FILENO, text, size);
                if (written <= 0) {
                    return;
                }
                text += written;
                size -= static_cast<std::size_t>(written);
            }
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

        int register_pool_guard(std::byte* guard_begin, std::byte* guard_end,
                                const char* name) noexcept {
            std::lock_guard lock(g_pool_registry_mutex);
            for (std::size_t index = 0; index != g_pool_registry.size(); ++index) {
                if (g_pool_registry[index].guard_begin.load(
                        std::memory_order_acquire) == 0) {
                    g_pool_registry[index].name.store(name,
                                                      std::memory_order_relaxed);
                    g_pool_registry[index].guard_end.store(
                        reinterpret_cast<std::uintptr_t>(guard_end),
                        std::memory_order_relaxed);
                    g_pool_registry[index].guard_begin.store(
                        reinterpret_cast<std::uintptr_t>(guard_begin),
                        std::memory_order_release);
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
                const std::uintptr_t begin =
                    entry.guard_begin.load(std::memory_order_acquire);
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

        void pool_signal_handler(int signal_number, siginfo_t* info,
                                 void*) noexcept {
            const char* pool_name =
                info == nullptr ? nullptr : find_pool_name(info->si_addr);

            write_all("doost: ");
            write_all(signal_name(signal_number));
            write_all(" while accessing ");
            if (pool_name != nullptr) {
                write_all("guard page of pool \"");
                write_all(pool_name);
                write_all("\"");
            }
            else {
                write_all("memory outside registered pools");
            }
            write_all("\n");

            struct sigaction action {};
            action.sa_handler = SIG_DFL;
            sigemptyset(&action.sa_mask);
            sigaction(signal_number, &action, nullptr);
            kill(getpid(), signal_number);
            _exit(128 + signal_number);
        }
#endif

#if defined(MAP_GROWSDOWN)
        constexpr int kMapGrowDown = MAP_GROWSDOWN;
#else
        constexpr int kMapGrowDown = 0;
#endif

#if defined(PROT_GROWSDOWN)
        constexpr int kProtGrowDown = PROT_GROWSDOWN;
#else
        constexpr int kProtGrowDown = 0;
#endif

#if defined(MAP_ANONYMOUS)
        constexpr int kAnonymousMap = MAP_ANONYMOUS;
#elif defined(MAP_ANON)
        constexpr int kAnonymousMap = MAP_ANON;
#else
        constexpr int kAnonymousMap = 0;
#endif

        constexpr int kPoolProtection = PROT_READ | PROT_WRITE;
        constexpr int kPoolMapFlags = MAP_PRIVATE | kMapGrowDown;
        constexpr int kGuardProtection = PROT_NONE | kProtGrowDown;

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

        std::size_t round_up(std::size_t value, std::size_t multiple) {
            const std::size_t remainder = value % multiple;
            if (remainder == 0) {
                return value;
            }

            const std::size_t increment = multiple - remainder;
            if (value > std::numeric_limits<std::size_t>::max() - increment) {
                throw std::length_error("pool size overflows size_t");
            }
            return value + increment;
        }

        bool is_power_of_two(std::size_t value) {
            return value != 0 && (value & (value - 1)) == 0;
        }
    } // namespace

    DownwardPoolStorage make_downward_pool_storage(std::size_t usable_bytes,
                                                   const char* overflow_name) {
        DownwardPoolStorage storage;
        storage.requested_bytes = usable_bytes;
        storage.page_size = system_page_size();

        if (usable_bytes == 0) {
            usable_bytes = 1;
        }

        storage.usable_bytes = round_up(usable_bytes, storage.page_size);
        storage.guard_bytes = storage.page_size;

        if (storage.usable_bytes >
            std::numeric_limits<std::size_t>::max() - storage.guard_bytes) {
            throw std::length_error("pool mapping size overflows size_t");
        }
        storage.mapping_bytes = storage.usable_bytes + storage.guard_bytes;

        void* mapping = mmap_anonymous(storage.mapping_bytes);
        if (mapping == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "mmap failed");
        }

        storage.mapping_begin = static_cast<std::byte*>(mapping);
        storage.lower_bound = storage.mapping_begin + storage.guard_bytes;
        storage.upper_bound = storage.mapping_begin + storage.mapping_bytes;

        if (mprotect(storage.mapping_begin, storage.guard_bytes,
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
            storage.mapping_begin, storage.lower_bound, storage.overflow_name);
#else
        static_cast<void>(overflow_name);
#endif

        return storage;
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

    void validate_downward_pool_allocation(std::size_t& bytes,
                                           std::size_t alignment) {
        if (!is_power_of_two(alignment)) {
            throw std::invalid_argument(
                "allocation alignment must be a power of two");
        }
        if (bytes == 0) {
            bytes = 1;
        }
    }

    std::uintptr_t align_downward_pool_cursor(std::uintptr_t current,
                                              std::size_t bytes,
                                              std::size_t alignment) noexcept {
        const std::uintptr_t raw = current - bytes;
        return raw & ~(static_cast<std::uintptr_t>(alignment) - 1U);
    }

    std::size_t downward_pool_used_bytes(
        std::uintptr_t cursor, const DownwardPoolStorage& storage) noexcept {
        const auto upper = reinterpret_cast<std::uintptr_t>(storage.upper_bound);
        if (cursor == 0 || upper == 0 || cursor >= upper) {
            return 0;
        }
        return static_cast<std::size_t>(upper - cursor);
    }

    std::size_t downward_pool_remaining_bytes(
        std::uintptr_t cursor, const DownwardPoolStorage& storage) noexcept {
        const auto lower = reinterpret_cast<std::uintptr_t>(storage.lower_bound);
        if (cursor == 0 || lower == 0 || cursor <= lower) {
            return 0;
        }
        return static_cast<std::size_t>(cursor - lower);
    }
} // namespace doost::detail

namespace doost {
    void install_pool_overflow_signal_handler() noexcept {
#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
        struct sigaction action {};
        action.sa_sigaction = detail::pool_signal_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_SIGINFO;
#if defined(SA_NODEFER)
        action.sa_flags |= SA_NODEFER;
#endif
        sigaction(SIGSEGV, &action, nullptr);
#if defined(SIGBUS)
        sigaction(SIGBUS, &action, nullptr);
#endif
#endif
    }
} // namespace doost
