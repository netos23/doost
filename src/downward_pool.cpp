#include "doost/downward_pool.hpp"

namespace doost {
    DownwardPool::DownwardPool(std::size_t usable_bytes,
                               std::size_t max_alloc_size)
        : storage_(detail::make_downward_pool_storage(usable_bytes,
                                                      max_alloc_size)),
          cursor_(detail::downward_pool_mapping_end(storage_)) {}

    DownwardPool::~DownwardPool() {
        release();
    }

    void DownwardPool::release() noexcept {
        detail::release_downward_pool_storage(storage_);
        cursor_ = nullptr;
    }
} // namespace doost
