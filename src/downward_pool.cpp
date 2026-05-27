#include "doost/downward_pool.hpp"

namespace doost {
    DownwardPool::DownwardPool(std::size_t usable_bytes,
                               const char* overflow_name)
        : storage_(detail::make_downward_pool_storage(usable_bytes,
                                                      overflow_name)),
          cursor_(detail::downward_pool_mapping_end(storage_)) {}

    DownwardPool::~DownwardPool() {
        release();
    }

    void DownwardPool::release() noexcept {
        detail::release_downward_pool_storage(storage_);
        cursor_ = nullptr;
    }
} // namespace doost
