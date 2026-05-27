#include "doost/parallel_memcpy.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace doost {
    namespace {
        constexpr std::size_t kMinParallelCopyBytes = 1024 * 1024;

        void copy_chunk(std::byte* dst, const std::byte* src, std::size_t size,
                        std::size_t chunk_count, std::size_t chunk_index) {
            const std::size_t base_size = size / chunk_count;
            const std::size_t extra = size % chunk_count;
            const std::size_t offset =
                chunk_index * base_size + std::min(chunk_index, extra);
            const std::size_t length =
                base_size + (chunk_index < extra ? 1U : 0U);

            std::memcpy(dst + offset, src + offset, length);
        }

        ParallelMemcpyPool& default_pool() {
            static ParallelMemcpyPool pool;
            return pool;
        }
    } // namespace

    struct ParallelMemcpyPool::Impl {
        mutable std::mutex mutex;
        std::condition_variable has_work;
        std::condition_variable work_done;
        std::vector<std::thread> workers;

        std::byte* dst = nullptr;
        const std::byte* src = nullptr;
        std::size_t size = 0;
        std::size_t chunk_count = 0;
        std::size_t next_chunk = 0;
        std::size_t finished_chunks = 0;
        std::size_t generation = 0;
        bool stopping = false;
    };

    ParallelMemcpyPool::ParallelMemcpyPool(std::size_t thread_count)
        : impl_(new Impl) {
        try {
            set_thread_count(thread_count);
        }
        catch (...) {
            stop_workers();
            delete impl_;
            throw;
        }
    }

    ParallelMemcpyPool::~ParallelMemcpyPool() {
        stop_workers();
        delete impl_;
    }

    void ParallelMemcpyPool::set_thread_count(std::size_t thread_count) {
        stop_workers();

        try {
            impl_->workers.reserve(thread_count);
            for (std::size_t index = 0; index != thread_count; ++index) {
                impl_->workers.emplace_back([this] {
                    worker_loop();
                });
            }
        }
        catch (...) {
            stop_workers();
            throw;
        }
    }

    std::size_t ParallelMemcpyPool::thread_count() const {
        std::lock_guard lock(impl_->mutex);
        return impl_->workers.size();
    }

    void* ParallelMemcpyPool::copy(void* dst, const void* src, std::size_t size) {
        if (size == 0) {
            return dst;
        }

        const std::size_t worker_count = thread_count();
        if (worker_count == 0 || size <= kMinParallelCopyBytes) {
            return std::memcpy(dst, src, size);
        }

        const std::size_t chunk_count = std::min(size, worker_count + 1);
        {
            std::lock_guard lock(impl_->mutex);
            impl_->dst = static_cast<std::byte*>(dst);
            impl_->src = static_cast<const std::byte*>(src);
            impl_->size = size;
            impl_->chunk_count = chunk_count;
            impl_->next_chunk = 0;
            impl_->finished_chunks = 0;
            ++impl_->generation;
        }

        impl_->has_work.notify_all();
        process_chunks();

        std::unique_lock lock(impl_->mutex);
        impl_->work_done.wait(lock, [this] {
            return impl_->finished_chunks == impl_->chunk_count;
        });
        return dst;
    }

    void ParallelMemcpyPool::stop_workers() noexcept {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->stopping = true;
            ++impl_->generation;
        }
        impl_->has_work.notify_all();

        for (std::thread& worker : impl_->workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        std::lock_guard lock(impl_->mutex);
        impl_->workers.clear();
        impl_->stopping = false;
    }

    void ParallelMemcpyPool::worker_loop() {
        std::size_t observed_generation = 0;
        {
            std::lock_guard lock(impl_->mutex);
            observed_generation = impl_->generation;
        }

        for (;;) {
            {
                std::unique_lock lock(impl_->mutex);
                impl_->has_work.wait(lock, [this, observed_generation] {
                    return impl_->stopping ||
                        impl_->generation != observed_generation;
                });

                if (impl_->stopping) {
                    return;
                }

                observed_generation = impl_->generation;
            }

            process_chunks();
        }
    }

    void ParallelMemcpyPool::process_chunks() {
        for (;;) {
            std::byte* dst = nullptr;
            const std::byte* src = nullptr;
            std::size_t size = 0;
            std::size_t chunk_count = 0;
            std::size_t chunk_index = 0;

            {
                std::lock_guard lock(impl_->mutex);
                if (impl_->next_chunk == impl_->chunk_count) {
                    return;
                }

                chunk_index = impl_->next_chunk++;
                dst = impl_->dst;
                src = impl_->src;
                size = impl_->size;
                chunk_count = impl_->chunk_count;
            }

            copy_chunk(dst, src, size, chunk_count, chunk_index);

            {
                std::lock_guard lock(impl_->mutex);
                ++impl_->finished_chunks;
                if (impl_->finished_chunks == impl_->chunk_count) {
                    impl_->work_done.notify_one();
                }
            }
        }
    }

    void set_parallel_memcpy_thread_count(std::size_t thread_count) {
        default_pool().set_thread_count(thread_count);
    }

    std::size_t parallel_memcpy_thread_count() {
        return default_pool().thread_count();
    }

    void* parallel_memcpy(void* dst, const void* src, std::size_t size) {
        return default_pool().copy(dst, src, size);
    }
} // namespace doost
