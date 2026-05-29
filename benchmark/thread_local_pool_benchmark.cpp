#include "list_allocator_benchmark.hpp"

#include "doost/downward_pool_allocator.hpp"

#include <vector>

int main(int argc, const char* argv[]) {
    using Pool = doost::DownwardPool;
    using Allocator = doost::SequentialPoolAllocator<unsigned, Pool>;
    using List = doost::List<unsigned, Allocator>;

    doost::install_pool_overflow_signal_handler();

    return doost::benchmark::run_benchmark(
        argc, argv, "thread-local-pools", List::node_size,
        [](const doost::benchmark::Options& options,
           std::size_t) -> doost::benchmark::PoolStats {
            const std::size_t storage_per_thread =
                List::required_storage(options.node_count);
            std::vector<std::size_t> usable(options.thread_count);
            std::vector<std::size_t> used(options.thread_count);

            doost::benchmark::run_threads(options.thread_count, [&](unsigned index) {
                Pool pool(storage_per_thread, sizeof(List::Node),
                          "thread-local-pool");
                List list{Allocator(pool)};
                doost::benchmark::fill_list(list, options.node_count);
                usable[index] = pool.usable_bytes();
                used[index] = pool.used_bytes();
                list.release_nodes();
                pool.release();
            });

            doost::benchmark::PoolStats stats;
            for (unsigned index = 0; index != options.thread_count; ++index) {
                stats.usable_storage += usable[index];
                stats.used_storage += used[index];
            }
            return stats;
        });
}
