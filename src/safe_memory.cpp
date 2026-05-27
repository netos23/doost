#include "doost/safe_memory.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <optional>

#include <setjmp.h>
#include <unistd.h>

namespace doost {
    namespace {
        sigjmp_buf g_read_jump_buffer;
        volatile sig_atomic_t g_read_active = 0;

        void safe_read_signal_handler(int signal_number, siginfo_t*, void*) noexcept {
            if (g_read_active != 0) {
                siglongjmp(g_read_jump_buffer, 1);
            }

            _exit(128 + signal_number);
        }

        struct TemporarySignalHandlers {
            struct sigaction previous_segv{};
#if defined(SIGBUS)
            struct sigaction previous_bus{};
#endif

            [[nodiscard]] bool install() noexcept {
                struct sigaction action{};
                action.sa_sigaction = safe_read_signal_handler;
                action.sa_flags = SA_SIGINFO;
                sigemptyset(&action.sa_mask);
                sigaddset(&action.sa_mask, SIGSEGV);
#if defined(SIGBUS)
                sigaddset(&action.sa_mask, SIGBUS);
#endif

                if (sigaction(SIGSEGV, &action, &previous_segv) != 0) {
                    return false;
                }

#if defined(SIGBUS)
                if (sigaction(SIGBUS, &action, &previous_bus) != 0) {
                    sigaction(SIGSEGV, &previous_segv, nullptr);
                    return false;
                }
#endif

                return true;
            }

            void restore() noexcept {
#if defined(SIGBUS)
                sigaction(SIGBUS, &previous_bus, nullptr);
#endif
                sigaction(SIGSEGV, &previous_segv, nullptr);
            }
        };
    } // namespace

    std::optional<std::uint8_t> safe_read_uint8(const std::uint8_t* p) noexcept {
        const int saved_errno = errno;

        TemporarySignalHandlers handlers;
        if (!handlers.install()) {
            errno = saved_errno;
            return std::nullopt;
        }

        std::optional<std::uint8_t> result;
        if (sigsetjmp(g_read_jump_buffer, 1) == 0) {
            g_read_active = 1;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const volatile std::uint8_t* readable = p;
            const std::uint8_t value = *readable;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            g_read_active = 0;
            result = value;
        }
        else {
            g_read_active = 0;
            result = std::nullopt;
        }

        handlers.restore();
        errno = saved_errno;
        return result;
    }
} // namespace doost
