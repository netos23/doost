#ifndef DOOST_PARALLEL_MEMCPY_HPP
#define DOOST_PARALLEL_MEMCPY_HPP

#include <cstddef>

namespace doost {
    class ParallelMemcpyPool {
    public:
        explicit ParallelMemcpyPool(std::size_t thread_count = 0);
        ~ParallelMemcpyPool();

        ParallelMemcpyPool(const ParallelMemcpyPool&) = delete;
        ParallelMemcpyPool& operator=(const ParallelMemcpyPool&) = delete;

        void set_thread_count(std::size_t thread_count);
        [[nodiscard]] std::size_t thread_count() const;

        void* copy(void* dst, const void* src, std::size_t size);

    private:
        void stop_workers() noexcept;
        void worker_loop();
        void process_chunks();

        struct Impl;
        Impl* impl_;
    };

    void set_parallel_memcpy_thread_count(std::size_t thread_count);
    [[nodiscard]] std::size_t parallel_memcpy_thread_count();
    void* parallel_memcpy(void* dst, const void* src, std::size_t size);
} // namespace doost

#endif // DOOST_PARALLEL_MEMCPY_HPP
