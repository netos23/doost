#include "doost/parallel_memcpy.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace doost {
    namespace {
        std::size_t default_thread_count() noexcept {
            const unsigned hardware_threads = std::thread::hardware_concurrency();
            if (hardware_threads <= 1) {
                return 0;
            }
            return static_cast<std::size_t>(hardware_threads - 1);
        }

        ParallelMemcpyPool& default_pool() {
            static ParallelMemcpyPool pool;
            return pool;
        }
    } // namespace

    struct ParallelMemcpyPool::Impl {
        struct WorkerState {
            std::byte* dst = nullptr;
            const std::byte* src = nullptr;
            std::size_t size = 0;
            std::atomic<std::size_t> generation{0};
        };

        std::vector<std::thread> workers;
        std::vector<std::unique_ptr<WorkerState>> worker_states;

        std::size_t scheduled_workers = 0;
        std::atomic<std::size_t> worker_count{0};
        std::atomic<std::size_t> finished_workers{0};
        std::atomic<bool> stopping{false};
    };

    ParallelMemcpyPool::ParallelMemcpyPool()
        : ParallelMemcpyPool(default_thread_count()) {}

    ParallelMemcpyPool::ParallelMemcpyPool(std::size_t thread_count)
        : impl_(new Impl) {
        try {
            start_workers(thread_count);
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

    void ParallelMemcpyPool::start_workers(std::size_t thread_count) {
        impl_->worker_states.reserve(thread_count);
        for (std::size_t index = 0; index != thread_count; ++index) {
            impl_->worker_states.push_back(
                std::make_unique<Impl::WorkerState>());
        }

        impl_->workers.reserve(thread_count);
        for (std::size_t index = 0; index != thread_count; ++index) {
            impl_->workers.emplace_back([this, index] {
                worker_loop(index);
            });
        }
        impl_->worker_count.store(thread_count, std::memory_order_release);
    }

    std::size_t ParallelMemcpyPool::thread_count() const {
        return impl_->worker_count.load(std::memory_order_acquire);
    }

    void* ParallelMemcpyPool::copy(void* dst, const void* src, std::size_t size) {
        if (size == 0) {
            return dst;
        }

        const std::size_t worker_count = thread_count();
        if (worker_count == 0 || size <= parallel_memcpy_min_parallel_bytes) {
            return std::memcpy(dst, src, size);
        }

        const std::size_t scheduled_workers = worker_count;
        const std::size_t part_count = scheduled_workers + 1;
        const std::size_t worker_size = size / part_count;
        auto* destination = static_cast<std::byte*>(dst);
        auto* source = static_cast<const std::byte*>(src);
        std::size_t offset = 0;

        impl_->scheduled_workers = scheduled_workers;
        impl_->finished_workers.store(0, std::memory_order_relaxed);

        for (std::size_t index = 0; index != scheduled_workers; ++index) {
            Impl::WorkerState& state = *impl_->worker_states[index];
            state.dst = destination + offset;
            state.src = source + offset;
            state.size = worker_size;
            offset += worker_size;
            state.generation.fetch_add(1, std::memory_order_acq_rel);
            state.generation.notify_one();
        }
        copy_bytes(destination + offset, source + offset, size - offset);

        std::size_t finished =
            impl_->finished_workers.load(std::memory_order_acquire);
        while (finished != scheduled_workers) {
            impl_->finished_workers.wait(finished, std::memory_order_acquire);
            finished = impl_->finished_workers.load(std::memory_order_acquire);
        }
        return dst;
    }

    void ParallelMemcpyPool::stop_workers() noexcept {
        impl_->stopping.store(true, std::memory_order_release);
        for (const auto& state : impl_->worker_states) {
            state->generation.fetch_add(1, std::memory_order_acq_rel);
            state->generation.notify_one();
        }

        for (std::thread& worker : impl_->workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        impl_->workers.clear();
        impl_->worker_states.clear();
        impl_->worker_count.store(0, std::memory_order_release);
        impl_->stopping.store(false, std::memory_order_release);
    }

    void ParallelMemcpyPool::worker_loop(std::size_t worker_index) {
        Impl::WorkerState& state = *impl_->worker_states[worker_index];
        std::size_t observed_generation = 0;

        for (;;) {
            if (impl_->stopping.load(std::memory_order_acquire)) {
                return;
            }

            std::size_t generation =
                state.generation.load(std::memory_order_acquire);
            while (generation == observed_generation) {
                if (impl_->stopping.load(std::memory_order_acquire)) {
                    return;
                }
                state.generation.wait(observed_generation,
                                      std::memory_order_acquire);
                generation = state.generation.load(std::memory_order_acquire);
            }

            observed_generation = generation;
            if (impl_->stopping.load(std::memory_order_acquire)) {
                return;
            }

            copy_bytes(state.dst, state.src, state.size);

            const std::size_t finished =
                impl_->finished_workers.fetch_add(
                    1, std::memory_order_acq_rel) + 1;
            if (finished == impl_->scheduled_workers) {
                impl_->finished_workers.notify_one();
            }
        }
    }

    void ParallelMemcpyPool::copy_bytes(std::byte* dst, const std::byte* src,
                                        std::size_t size) {
        if (size == 0) {
            return;
        }

        std::memcpy(dst, src, size);
    }

    std::size_t parallel_memcpy_thread_count() {
        return default_pool().thread_count();
    }

    void* parallel_memcpy(void* dst, const void* src, std::size_t size) {
        return default_pool().copy(dst, src, size);
    }
} // namespace doost
