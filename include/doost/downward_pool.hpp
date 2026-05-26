#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>

namespace doost {
    class DownwardPool {
    public:
        explicit DownwardPool(std::size_t usable_bytes);
        ~DownwardPool();

        DownwardPool(const DownwardPool&) = delete;
        DownwardPool& operator=(const DownwardPool&) = delete;

        DownwardPool(DownwardPool&& other) noexcept;
        DownwardPool& operator=(DownwardPool&& other) noexcept;

        [[nodiscard]] inline void* allocate(std::size_t bytes,
                                            std::size_t alignment) {
            if (mapping_begin_ == nullptr) {
                throw std::bad_alloc();
            }
            if (!is_power_of_two(alignment)) {
                throw std::invalid_argument(
                    "allocation alignment must be a power of two");
            }
            if (bytes == 0) {
                bytes = 1;
            }

            const auto current = reinterpret_cast<std::uintptr_t>(cursor_);
            const std::uintptr_t raw = current - bytes;
            const std::uintptr_t mask =
                static_cast<std::uintptr_t>(alignment) - 1U;
            const std::uintptr_t aligned = raw & ~mask;

            cursor_ = reinterpret_cast<std::byte*>(aligned);
            return cursor_;
        }

        inline void reset() noexcept {
            cursor_ = mapping_begin_ == nullptr ? nullptr
                                                : mapping_begin_ + mapping_bytes_;
        }

        void release() noexcept;

        [[nodiscard]] inline std::size_t requested_bytes() const noexcept {
            return requested_bytes_;
        }

        [[nodiscard]] inline std::size_t usable_bytes() const noexcept {
            return mapping_bytes_ - guard_bytes();
        }

        [[nodiscard]] inline std::size_t used_bytes() const noexcept {
            if (cursor_ == nullptr) {
                return 0;
            }
            const std::byte* upper_bound = mapping_begin_ + mapping_bytes_;
            return static_cast<std::size_t>(upper_bound - cursor_);
        }

        [[nodiscard]] inline std::size_t remaining_bytes() const noexcept {
            if (cursor_ == nullptr) {
                return 0;
            }
            const std::byte* lower_bound = mapping_begin_ + guard_bytes();
            return static_cast<std::size_t>(cursor_ - lower_bound);
        }

        [[nodiscard]] inline std::size_t guard_bytes() const noexcept {
            return page_size_;
        }

        [[nodiscard]] inline std::size_t page_size() const noexcept {
            return page_size_;
        }

    private:
        [[nodiscard]] static inline bool is_power_of_two(
            std::size_t value) noexcept {
            return value != 0 && (value & (value - 1)) == 0;
        }

        void move_from(DownwardPool& other) noexcept;

        std::byte* mapping_begin_ = nullptr;
        std::byte* cursor_ = nullptr;
        std::size_t requested_bytes_ = 0;
        std::size_t mapping_bytes_ = 0;
        std::size_t page_size_ = 0;
    };
} // namespace doost
