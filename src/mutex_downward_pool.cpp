#include "doost/mutex_downward_pool.hpp"

namespace doost {
    MutexDownwardPool::MutexDownwardPool(std::size_t usable_bytes,
                                         const char* overflow_name)
        : pool_(usable_bytes, overflow_name) {}

    void* MutexDownwardPool::allocate(std::size_t bytes,
                                      std::size_t alignment) {
        std::lock_guard lock(mutex_);
        return pool_.allocate(bytes, alignment);
    }

    void MutexDownwardPool::deallocate(void* ptr, std::size_t bytes,
                                       std::size_t alignment) noexcept {
        std::lock_guard lock(mutex_);
        pool_.deallocate(ptr, bytes, alignment);
    }

    void MutexDownwardPool::reset() noexcept {
        std::lock_guard lock(mutex_);
        pool_.reset();
    }

    void MutexDownwardPool::release() noexcept {
        std::lock_guard lock(mutex_);
        pool_.release();
    }

    std::size_t MutexDownwardPool::requested_bytes() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.requested_bytes();
    }

    std::size_t MutexDownwardPool::usable_bytes() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.usable_bytes();
    }

    std::size_t MutexDownwardPool::used_bytes() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.used_bytes();
    }

    std::size_t MutexDownwardPool::remaining_bytes() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.remaining_bytes();
    }

    std::size_t MutexDownwardPool::guard_bytes() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.guard_bytes();
    }

    std::size_t MutexDownwardPool::page_size() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.page_size();
    }
} // namespace doost
