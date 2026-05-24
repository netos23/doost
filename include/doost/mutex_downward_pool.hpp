#ifndef DOOST_MUTEX_DOWNWARD_POOL_HPP
#define DOOST_MUTEX_DOWNWARD_POOL_HPP

#include "doost/downward_pool.hpp"

#include <cstddef>
#include <mutex>

namespace doost {
    class MutexDownwardPool {
    public:
        explicit MutexDownwardPool(std::size_t usable_bytes,
                                   const char* overflow_name =
                                       "global-mutex-pool");

        MutexDownwardPool(const MutexDownwardPool&) = delete;
        MutexDownwardPool& operator=(const MutexDownwardPool&) = delete;

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);
        void deallocate(void* ptr, std::size_t bytes,
                        std::size_t alignment) noexcept;

        void reset() noexcept;
        void release() noexcept;

        [[nodiscard]] std::size_t requested_bytes() const noexcept;
        [[nodiscard]] std::size_t usable_bytes() const noexcept;
        [[nodiscard]] std::size_t used_bytes() const noexcept;
        [[nodiscard]] std::size_t remaining_bytes() const noexcept;
        [[nodiscard]] std::size_t guard_bytes() const noexcept;
        [[nodiscard]] std::size_t page_size() const noexcept;

    private:
        DownwardPool pool_;
        mutable std::mutex mutex_;
    };
} // namespace doost

#endif // DOOST_MUTEX_DOWNWARD_POOL_HPP
