#ifndef DOOST_NONBLOCKING_DOWNWARD_POOL_HPP
#define DOOST_NONBLOCKING_DOWNWARD_POOL_HPP

#include "doost/detail/downward_pool_storage.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace doost {
    class NonblockingDownwardPool {
    public:
        explicit NonblockingDownwardPool(std::size_t usable_bytes,
                                         const char* overflow_name =
                                             "global-nonblocking-pool");
        ~NonblockingDownwardPool();

        NonblockingDownwardPool(const NonblockingDownwardPool&) = delete;
        NonblockingDownwardPool& operator=(const NonblockingDownwardPool&) = delete;

        NonblockingDownwardPool(NonblockingDownwardPool&& other) noexcept;
        NonblockingDownwardPool& operator=(
            NonblockingDownwardPool&& other) noexcept;

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);

        void reset() noexcept;
        void release() noexcept;

        [[nodiscard]] std::size_t usable_bytes() const noexcept;
        [[nodiscard]] std::size_t used_bytes() const noexcept;

    private:
        void move_from(NonblockingDownwardPool& other) noexcept;

        detail::DownwardPoolStorage storage_;
        std::atomic<std::uintptr_t> cursor_{0};
    };

    inline NonblockingDownwardPool::NonblockingDownwardPool(
        NonblockingDownwardPool&& other) noexcept {
        move_from(other);
    }

    inline NonblockingDownwardPool& NonblockingDownwardPool::operator=(
        NonblockingDownwardPool&& other) noexcept {
        if (this != &other) {
            release();
            move_from(other);
        }
        return *this;
    }

    [[nodiscard]] inline void* NonblockingDownwardPool::allocate(
        std::size_t bytes, std::size_t alignment) {
        std::uintptr_t current = cursor_.load(std::memory_order_relaxed);
        for (;;) {
            const std::uintptr_t aligned =
                detail::align_downward_pool_cursor(current, bytes, alignment);
            if (cursor_.compare_exchange_weak(current, aligned,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
                return reinterpret_cast<void*>(aligned);
            }
        }
    }

    inline void NonblockingDownwardPool::reset() noexcept {
        cursor_.store(
            reinterpret_cast<std::uintptr_t>(
                detail::downward_pool_mapping_end(storage_)),
            std::memory_order_release);
    }

    [[nodiscard]] inline std::size_t
    NonblockingDownwardPool::usable_bytes() const noexcept {
        return detail::downward_pool_usable_bytes(storage_);
    }

    [[nodiscard]] inline std::size_t NonblockingDownwardPool::used_bytes()
    const noexcept {
        return detail::downward_pool_used_bytes(
            cursor_.load(std::memory_order_relaxed), storage_);
    }

    inline void NonblockingDownwardPool::move_from(
        NonblockingDownwardPool& other) noexcept {
        storage_ = other.storage_;
        cursor_.store(other.cursor_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);

        other.storage_ = {};
        other.cursor_.store(0, std::memory_order_relaxed);
    }
} // namespace doost

#endif // DOOST_NONBLOCKING_DOWNWARD_POOL_HPP
