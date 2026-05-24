#include "doost/nonblocking_downward_pool.hpp"

#include <new>

namespace doost {
    NonblockingDownwardPool::NonblockingDownwardPool(
        std::size_t usable_bytes, const char* overflow_name)
        : storage_(detail::make_downward_pool_storage(usable_bytes,
                                                      overflow_name)),
          cursor_(reinterpret_cast<std::uintptr_t>(storage_.upper_bound)) {}

    NonblockingDownwardPool::~NonblockingDownwardPool() {
        release();
    }

    NonblockingDownwardPool::NonblockingDownwardPool(
        NonblockingDownwardPool&& other) noexcept {
        move_from(other);
    }

    NonblockingDownwardPool& NonblockingDownwardPool::operator=(
        NonblockingDownwardPool&& other) noexcept {
        if (this != &other) {
            release();
            move_from(other);
        }
        return *this;
    }

    void* NonblockingDownwardPool::allocate(std::size_t bytes,
                                            std::size_t alignment) {
        if (storage_.mapping_begin == nullptr) {
            throw std::bad_alloc();
        }
        detail::validate_downward_pool_allocation(bytes, alignment);

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

    void NonblockingDownwardPool::deallocate(
        void*, std::size_t, std::size_t) noexcept {}

    void NonblockingDownwardPool::reset() noexcept {
        cursor_.store(reinterpret_cast<std::uintptr_t>(storage_.upper_bound),
                      std::memory_order_release);
    }

    void NonblockingDownwardPool::release() noexcept {
        detail::release_downward_pool_storage(storage_);
        cursor_.store(0, std::memory_order_relaxed);
    }

    std::size_t NonblockingDownwardPool::requested_bytes() const noexcept {
        return storage_.requested_bytes;
    }

    std::size_t NonblockingDownwardPool::usable_bytes() const noexcept {
        return storage_.usable_bytes;
    }

    std::size_t NonblockingDownwardPool::used_bytes() const noexcept {
        return detail::downward_pool_used_bytes(
            cursor_.load(std::memory_order_relaxed), storage_);
    }

    std::size_t NonblockingDownwardPool::remaining_bytes() const noexcept {
        return detail::downward_pool_remaining_bytes(
            cursor_.load(std::memory_order_relaxed), storage_);
    }

    std::size_t NonblockingDownwardPool::guard_bytes() const noexcept {
        return storage_.guard_bytes;
    }

    std::size_t NonblockingDownwardPool::page_size() const noexcept {
        return storage_.page_size;
    }

    void NonblockingDownwardPool::move_from(
        NonblockingDownwardPool& other) noexcept {
        storage_ = other.storage_;
        cursor_.store(other.cursor_.load(std::memory_order_relaxed),
                      std::memory_order_relaxed);

        other.storage_ = {};
        other.cursor_.store(0, std::memory_order_relaxed);
    }
} // namespace doost
