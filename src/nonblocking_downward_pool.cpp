#include "doost/nonblocking_downward_pool.hpp"

namespace doost {
    NonblockingDownwardPool::NonblockingDownwardPool(
        std::size_t usable_bytes, std::size_t max_alloc_size,
        const char* overflow_name)
        : storage_(detail::make_downward_pool_storage(usable_bytes,
                                                      max_alloc_size,
                                                      overflow_name)),
          cursor_(reinterpret_cast<std::uintptr_t>(
              detail::downward_pool_mapping_end(storage_))) {}

    NonblockingDownwardPool::~NonblockingDownwardPool() {
        release();
    }

    void NonblockingDownwardPool::release() noexcept {
        detail::release_downward_pool_storage(storage_);
        cursor_.store(0, std::memory_order_relaxed);
    }
} // namespace doost
