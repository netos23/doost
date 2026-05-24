#include "list_allocator_benchmark.hpp"

int main(int argc, const char* argv[]) {
    using List = doost::List<unsigned>;

    return doost::benchmark::run_benchmark(
        argc, argv, "standard-new-delete", List::node_size,
        [](const doost::benchmark::Options& options,
           std::size_t) -> doost::benchmark::PoolStats {
            doost::benchmark::run_threads(options.thread_count, [&](unsigned) {
                List list;
                doost::benchmark::fill_list(list, options.node_count);
                list.clear();
            });
            return {};
        });
}
