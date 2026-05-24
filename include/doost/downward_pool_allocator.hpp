#pragma once

#include "doost/downward_pool.hpp"

#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>

namespace doost {
    template <class T>
    class DownwardPoolAllocator {
    public:
        using value_type = T;
        using size_type = std::size_t;
        using difference_type = std::ptrdiff_t;
        using propagate_on_container_move_assignment = std::true_type;
        using is_always_equal = std::false_type;

        template <class U>
        struct rebind {
            using other = DownwardPoolAllocator<U>;
        };

        DownwardPoolAllocator() noexcept = default;

        explicit DownwardPoolAllocator(DownwardPool& pool) noexcept
            : pool_(&pool) {}

        template <class U>
        DownwardPoolAllocator(const DownwardPoolAllocator<U>& other) noexcept
            : pool_(other.pool()) {}

        [[nodiscard]] T* allocate(std::size_t n) {
            if (pool_ == nullptr) {
                throw std::bad_alloc();
            }
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T)) {
                throw std::bad_alloc();
            }
            return static_cast<T*>(pool_->allocate(n * sizeof(T), alignof(T)));
        }

        void deallocate(T* ptr, std::size_t n) noexcept {
            if (pool_ != nullptr) {
                pool_->deallocate(ptr, n * sizeof(T), alignof(T));
            }
        }

        [[nodiscard]] DownwardPool* pool() const noexcept {
            return pool_;
        }

    private:
        template <class U>
        friend class DownwardPoolAllocator;

        DownwardPool* pool_ = nullptr;
    };

    template <class T, class U>
    [[nodiscard]] bool operator==(const DownwardPoolAllocator<T>& lhs,
                                  const DownwardPoolAllocator<U>& rhs) noexcept {
        return lhs.pool() == rhs.pool();
    }

    template <class T, class U>
    [[nodiscard]] bool operator!=(const DownwardPoolAllocator<T>& lhs,
                                  const DownwardPoolAllocator<U>& rhs) noexcept {
        return !(lhs == rhs);
    }
} // namespace doost
