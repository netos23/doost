#ifndef DOOST_DOWNWARD_POOL_HPP
#define DOOST_DOWNWARD_POOL_HPP

#include "doost/detail/downward_pool_storage.hpp"

#include <cstddef>
#include <cstdint>

namespace doost {
    class DownwardPool {
    public:
        explicit DownwardPool(std::size_t usable_bytes,
                              std::size_t max_alloc_size,
                              const char* overflow_name = "downward-pool");
        ~DownwardPool();

        DownwardPool(const DownwardPool&) = delete;
        DownwardPool& operator=(const DownwardPool&) = delete;

        DownwardPool(DownwardPool&& other) noexcept;
        DownwardPool& operator=(DownwardPool&& other) noexcept;

        [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);

        void reset() noexcept;
        void release() noexcept;

        [[nodiscard]] std::size_t usable_bytes() const noexcept;
        [[nodiscard]] std::size_t used_bytes() const noexcept;

    private:
        void move_from(DownwardPool& other) noexcept;

        detail::DownwardPoolStorage storage_;
        std::byte* cursor_ = nullptr;
    };

    inline DownwardPool::DownwardPool(DownwardPool&& other) noexcept {
        move_from(other);
    }

    inline DownwardPool& DownwardPool::operator=(
        DownwardPool&& other) noexcept {
        if (this != &other) {
            release();
            move_from(other);
        }
        return *this;
    }

    [[nodiscard]] inline void* DownwardPool::allocate(
        std::size_t bytes, std::size_t alignment) {
        const auto current = reinterpret_cast<std::uintptr_t>(cursor_);
        cursor_ = reinterpret_cast<std::byte*>(
            detail::align_downward_pool_cursor(current, bytes, alignment));
        return cursor_;
    }

    inline void DownwardPool::reset() noexcept {
        cursor_ = detail::downward_pool_mapping_end(storage_);
    }

    [[nodiscard]] inline std::size_t DownwardPool::usable_bytes()
    const noexcept {
        return detail::downward_pool_usable_bytes(storage_);
    }

    [[nodiscard]] inline std::size_t DownwardPool::used_bytes() const noexcept {
        return detail::downward_pool_used_bytes(
            reinterpret_cast<std::uintptr_t>(cursor_), storage_);
    }

    inline void DownwardPool::move_from(DownwardPool& other) noexcept {
        storage_ = other.storage_;
        cursor_ = other.cursor_;

        other.storage_ = {};
        other.cursor_ = nullptr;
    }

    void install_pool_overflow_signal_handler() noexcept;
} // namespace doost

#endif // DOOST_DOWNWARD_POOL_HPP
