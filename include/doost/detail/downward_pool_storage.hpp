#ifndef DOOST_DETAIL_DOWNWARD_POOL_STORAGE_HPP
#define DOOST_DETAIL_DOWNWARD_POOL_STORAGE_HPP

#include <cstddef>
#include <cstdint>

namespace doost::detail {
    struct DownwardPoolStorage {
        std::byte* mapping_begin = nullptr;
        std::size_t mapping_bytes = 0;
    };

    [[nodiscard]] DownwardPoolStorage make_downward_pool_storage(
        std::size_t usable_bytes,
        std::size_t max_alloc_size
    );
    void release_downward_pool_storage(DownwardPoolStorage& storage) noexcept;

    [[nodiscard]] inline std::byte* downward_pool_mapping_end(
        const DownwardPoolStorage& storage) noexcept {
        if (storage.mapping_begin == nullptr) {
            return nullptr;
        }
        return storage.mapping_begin + storage.mapping_bytes;
    }

    [[nodiscard]] std::size_t downward_pool_usable_bytes(
        const DownwardPoolStorage& storage) noexcept;

    [[nodiscard]] inline std::uintptr_t align_downward_pool_cursor(
        std::uintptr_t current, std::size_t bytes,
        std::size_t alignment) noexcept {
        const std::uintptr_t raw = current - bytes;
        return raw & ~(static_cast<std::uintptr_t>(alignment) - 1U);
    }

    [[nodiscard]] inline std::size_t downward_pool_used_bytes(
        std::uintptr_t cursor, const DownwardPoolStorage& storage) noexcept {
        const auto upper =
            reinterpret_cast<std::uintptr_t>(downward_pool_mapping_end(storage));
        if (cursor == 0 || upper == 0 || cursor >= upper) {
            return 0;
        }
        return static_cast<std::size_t>(upper - cursor);
    }
} // namespace doost::detail

#endif // DOOST_DETAIL_DOWNWARD_POOL_STORAGE_HPP
