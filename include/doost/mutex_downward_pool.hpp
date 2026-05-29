#ifndef DOOST_MUTEX_DOWNWARD_POOL_HPP
#define DOOST_MUTEX_DOWNWARD_POOL_HPP

#include "doost/downward_pool.hpp"

#include <cstddef>
#include <mutex>

namespace doost {
    class MutexDownwardPool {
    public:
        explicit MutexDownwardPool(std::size_t usable_bytes,
                                   std::size_t max_alloc_size,
                                   const char* overflow_name =
                                       "global-mutex-pool");

        MutexDownwardPool(const MutexDownwardPool&) = delete;
        MutexDownwardPool& operator=(const MutexDownwardPool&) = delete;

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);

        void reset() noexcept;
        void release() noexcept;

        [[nodiscard]] std::size_t usable_bytes() const noexcept;
        [[nodiscard]] std::size_t used_bytes() const noexcept;

    private:
        DownwardPool pool_;
        mutable std::mutex mutex_;
    };

    inline MutexDownwardPool::MutexDownwardPool(
        std::size_t usable_bytes,
        std::size_t max_alloc_size,
        const char* overflow_name)
        : pool_(usable_bytes, max_alloc_size, overflow_name) {}

    [[nodiscard]] inline void* MutexDownwardPool::allocate(
        std::size_t bytes, std::size_t alignment) {
        std::lock_guard lock(mutex_);
        return pool_.allocate(bytes, alignment);
    }

    inline void MutexDownwardPool::reset() noexcept {
        std::lock_guard lock(mutex_);
        pool_.reset();
    }

    inline void MutexDownwardPool::release() noexcept {
        std::lock_guard lock(mutex_);
        pool_.release();
    }

    [[nodiscard]] inline std::size_t MutexDownwardPool::usable_bytes()
    const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.usable_bytes();
    }

    [[nodiscard]] inline std::size_t MutexDownwardPool::used_bytes()
    const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.used_bytes();
    }
} // namespace doost

#endif // DOOST_MUTEX_DOWNWARD_POOL_HPP
