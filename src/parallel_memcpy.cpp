#include "doost/parallel_memcpy.hpp"

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace doost {
    namespace {
        constexpr std::size_t kMaxDefaultThreadCount = 8;

        std::size_t default_thread_count() noexcept {
            const unsigned hardware_threads = std::thread::hardware_concurrency();
            if (hardware_threads <= 1) {
                return 0;
            }

            const std::size_t worker_threads =
                static_cast<std::size_t>(hardware_threads - 1);
            return worker_threads < kMaxDefaultThreadCount
                       ? worker_threads
                       : kMaxDefaultThreadCount;
        }

        ParallelMemcpyPool& default_pool() {
            static ParallelMemcpyPool pool;
            return pool;
        }
    } // namespace

    struct ParallelMemcpyPool::Impl {
        struct alignas(64) WorkerState {
            std::byte* dst = nullptr;
            const std::byte* src = nullptr;
            std::size_t size = 0;
            std::atomic<std::size_t> completed_generation{0};
        };

        std::vector<std::thread> workers;
        std::unique_ptr<WorkerState[]> worker_states;

        std::size_t worker_count = 0;
        std::atomic<std::size_t> start_generation{0};
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
        impl_->worker_count = thread_count;
        impl_->worker_states = std::make_unique<Impl::WorkerState[]>(
            thread_count);

        impl_->workers.reserve(thread_count);
        for (std::size_t index = 0; index != thread_count; ++index) {
            impl_->workers.emplace_back([this, index] {
                worker_loop(index);
            });
        }
    }

    std::size_t ParallelMemcpyPool::thread_count() const {
        return impl_->worker_count;
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

        for (std::size_t index = 0; index != scheduled_workers; ++index) {
            Impl::WorkerState& state = impl_->worker_states[index];
            state.dst = destination + offset;
            state.src = source + offset;
            state.size = worker_size;
            offset += worker_size;
        }

        const std::size_t generation =
            impl_->start_generation.load(std::memory_order_relaxed) + 1;
        impl_->start_generation.store(generation, std::memory_order_release);
        impl_->start_generation.notify_all();

        if (size - offset > 0) {
            std::memcpy(destination + offset, source + offset, size - offset);
        }

        for (std::size_t index = 0; index != scheduled_workers; ++index) {
            const Impl::WorkerState& state = impl_->worker_states[index];
            std::size_t completed =
                state.completed_generation.load(std::memory_order_acquire);
            while (completed != generation) {
                state.completed_generation.wait(
                    completed, std::memory_order_acquire);
                completed =
                    state.completed_generation.load(std::memory_order_acquire);
            }
        }
        return dst;
    }

    void ParallelMemcpyPool::stop_workers() noexcept {
        impl_->stopping.store(true, std::memory_order_release);
        impl_->start_generation.fetch_add(1, std::memory_order_acq_rel);
        impl_->start_generation.notify_all();

        for (std::thread& worker : impl_->workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        impl_->workers.clear();
        impl_->worker_states.reset();
        impl_->worker_count = 0;
        impl_->stopping.store(false, std::memory_order_release);
    }

    void ParallelMemcpyPool::worker_loop(std::size_t worker_index) {
        Impl::WorkerState& state = impl_->worker_states[worker_index];
        std::size_t observed_generation = 0;

        for (;;) {
            std::size_t generation =
                impl_->start_generation.load(std::memory_order_acquire);
            while (generation == observed_generation) {
                if (impl_->stopping.load(std::memory_order_acquire)) {
                    return;
                }
                impl_->start_generation.wait(observed_generation,
                                             std::memory_order_acquire);
                generation =
                    impl_->start_generation.load(std::memory_order_acquire);
            }

            observed_generation = generation;
            if (impl_->stopping.load(std::memory_order_acquire)) {
                return;
            }

            std::memcpy(state.dst, state.src, state.size);
            state.completed_generation.store(generation,
                                             std::memory_order_release);
            state.completed_generation.notify_one();
        }
    }


    std::size_t parallel_memcpy_thread_count() {
        return default_pool().thread_count();
    }

    void* parallel_memcpy(void* dst, const void* src, std::size_t size) {
        return default_pool().copy(dst, src, size);
    }
} // namespace doost
