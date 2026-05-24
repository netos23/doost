#ifndef DOOST_DETAIL_DOWNWARD_POOL_STORAGE_HPP
#define DOOST_DETAIL_DOWNWARD_POOL_STORAGE_HPP

#include <cstddef>
#include <cstdint>

namespace doost::detail {
    struct DownwardPoolStorage {
        std::byte* mapping_begin = nullptr;
        std::byte* lower_bound = nullptr;
        std::byte* upper_bound = nullptr;
        std::size_t requested_bytes = 0;
        std::size_t usable_bytes = 0;
        std::size_t guard_bytes = 0;
        std::size_t mapping_bytes = 0;
        std::size_t page_size = 0;
#if defined(DOOST_ENABLE_SIGSEGV_HANDLER)
        int registry_slot = -1;
        const char* overflow_name = nullptr;
#endif
    };

    [[nodiscard]] DownwardPoolStorage make_downward_pool_storage(
        std::size_t usable_bytes, const char* overflow_name);
    void release_downward_pool_storage(DownwardPoolStorage& storage) noexcept;

    void validate_downward_pool_allocation(std::size_t& bytes,
                                           std::size_t alignment);
    [[nodiscard]] std::uintptr_t align_downward_pool_cursor(
        std::uintptr_t current, std::size_t bytes,
        std::size_t alignment) noexcept;
    [[nodiscard]] std::size_t downward_pool_used_bytes(
        std::uintptr_t cursor, const DownwardPoolStorage& storage) noexcept;
    [[nodiscard]] std::size_t downward_pool_remaining_bytes(
        std::uintptr_t cursor, const DownwardPoolStorage& storage) noexcept;
} // namespace doost::detail

#endif // DOOST_DETAIL_DOWNWARD_POOL_STORAGE_HPP
