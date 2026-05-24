#ifndef DOOST_DOWNWARD_POOL_HPP
#define DOOST_DOWNWARD_POOL_HPP

#include "doost/detail/downward_pool_storage.hpp"

#include <cstddef>

namespace doost {
    class DownwardPool {
    public:
        explicit DownwardPool(std::size_t usable_bytes,
                              const char* overflow_name = "downward-pool");
        ~DownwardPool();

        DownwardPool(const DownwardPool&) = delete;
        DownwardPool& operator=(const DownwardPool&) = delete;

        DownwardPool(DownwardPool&& other) noexcept;
        DownwardPool& operator=(DownwardPool&& other) noexcept;

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);
        void deallocate(void* ptr, std::size_t bytes, std::size_t alignment) noexcept;

        void reset() noexcept;
        void release() noexcept;

        [[nodiscard]] std::size_t requested_bytes() const noexcept;
        [[nodiscard]] std::size_t usable_bytes() const noexcept;
        [[nodiscard]] std::size_t used_bytes() const noexcept;
        [[nodiscard]] std::size_t remaining_bytes() const noexcept;
        [[nodiscard]] std::size_t guard_bytes() const noexcept;
        [[nodiscard]] std::size_t page_size() const noexcept;

    private:
        void move_from(DownwardPool& other) noexcept;

        detail::DownwardPoolStorage storage_;
        std::byte* cursor_ = nullptr;
    };

    void install_pool_overflow_signal_handler() noexcept;
} // namespace doost

#endif // DOOST_DOWNWARD_POOL_HPP
