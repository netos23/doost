#include "list_allocator_benchmark.hpp"

#include "doost/downward_pool_allocator.hpp"
#include "doost/nonblocking_downward_pool.hpp"

int main(int argc, const char* argv[]) {
    using Pool = doost::NonblockingDownwardPool;
    using Allocator = doost::SequentialPoolAllocator<unsigned, Pool>;
    using List = doost::List<unsigned, Allocator>;

    doost::install_pool_overflow_signal_handler();

    return doost::benchmark::run_benchmark(
        argc, argv, "global-nonblocking-pool", List::node_size,
        [](const doost::benchmark::Options& options,
           std::size_t node_storage_required) -> doost::benchmark::PoolStats {
            Pool pool(node_storage_required, sizeof(List::Node),
                      "global-nonblocking-pool");

            doost::benchmark::run_threads(options.thread_count, [&](unsigned) {
                List list{Allocator(pool)};
                doost::benchmark::fill_list(list, options.node_count);
                list.release_nodes();
            });

            const doost::benchmark::PoolStats stats{
                pool.usable_bytes(),
                pool.used_bytes()
            };
            pool.release();
            return stats;
        });
}
