#pragma once

#include <cstddef>

namespace doost {
    class DownwardPool {
    public:
        explicit DownwardPool(std::size_t usable_bytes);
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

        std::byte* mapping_begin_ = nullptr;
        std::byte* lower_bound_ = nullptr;
        std::byte* cursor_ = nullptr;
        std::byte* upper_bound_ = nullptr;
        std::size_t requested_bytes_ = 0;
        std::size_t usable_bytes_ = 0;
        std::size_t guard_bytes_ = 0;
        std::size_t mapping_bytes_ = 0;
        std::size_t page_size_ = 0;
    };
} // namespace doost
