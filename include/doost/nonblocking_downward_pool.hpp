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
        void move_from(NonblockingDownwardPool& other) noexcept;

        detail::DownwardPoolStorage storage_;
        std::atomic<std::uintptr_t> cursor_{0};
    };
} // namespace doost

#endif // DOOST_NONBLOCKING_DOWNWARD_POOL_HPP
