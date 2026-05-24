#include "doost/downward_pool.hpp"

#include <cstdint>
#include <new>

namespace doost {
    DownwardPool::DownwardPool(std::size_t usable_bytes,
                               const char* overflow_name)
        : storage_(detail::make_downward_pool_storage(usable_bytes,
                                                      overflow_name)),
          cursor_(storage_.upper_bound) {}

    DownwardPool::~DownwardPool() {
        release();
    }

    DownwardPool::DownwardPool(DownwardPool&& other) noexcept {
        move_from(other);
    }

    DownwardPool& DownwardPool::operator=(DownwardPool&& other) noexcept {
        if (this != &other) {
            release();
            move_from(other);
        }
        return *this;
    }

    void* DownwardPool::allocate(std::size_t bytes, std::size_t alignment) {
        if (storage_.mapping_begin == nullptr) {
            throw std::bad_alloc();
        }
        detail::validate_downward_pool_allocation(bytes, alignment);

        const auto current = reinterpret_cast<std::uintptr_t>(cursor_);
        const std::uintptr_t aligned =
            detail::align_downward_pool_cursor(current, bytes, alignment);

        cursor_ = reinterpret_cast<std::byte*>(aligned);
        return cursor_;
    }

    void DownwardPool::deallocate(void*, std::size_t, std::size_t) noexcept {}

    void DownwardPool::reset() noexcept {
        cursor_ = storage_.upper_bound;
    }

    void DownwardPool::release() noexcept {
        detail::release_downward_pool_storage(storage_);
        cursor_ = nullptr;
    }

    std::size_t DownwardPool::requested_bytes() const noexcept {
        return storage_.requested_bytes;
    }

    std::size_t DownwardPool::usable_bytes() const noexcept {
        return storage_.usable_bytes;
    }

    std::size_t DownwardPool::used_bytes() const noexcept {
        return detail::downward_pool_used_bytes(
            reinterpret_cast<std::uintptr_t>(cursor_), storage_);
    }

    std::size_t DownwardPool::remaining_bytes() const noexcept {
        return detail::downward_pool_remaining_bytes(
            reinterpret_cast<std::uintptr_t>(cursor_), storage_);
    }

    std::size_t DownwardPool::guard_bytes() const noexcept {
        return storage_.guard_bytes;
    }

    std::size_t DownwardPool::page_size() const noexcept {
        return storage_.page_size;
    }

    void DownwardPool::move_from(DownwardPool& other) noexcept {
        storage_ = other.storage_;
        cursor_ = other.cursor_;

        other.storage_ = {};
        other.cursor_ = nullptr;
    }
} // namespace doost
