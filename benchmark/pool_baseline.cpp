#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sys/resource.h>
#include <sys/time.h>

namespace {

constexpr unsigned default_node_count = 10000000;

static void get_usage(struct rusage& usage) {
  if (getrusage(RUSAGE_SELF, &usage)) {
    perror("Cannot get usage");
    exit(EXIT_FAILURE);
  }
}

static uint64_t max_rss_bytes(const struct rusage& usage) {
#if defined(__APPLE__) && defined(__MACH__)
  return static_cast<uint64_t>(usage.ru_maxrss);
#else
  return static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
#endif
}

static uint64_t user_time_us(const struct rusage& start,
                             const struct rusage& finish) {
  auto seconds = finish.ru_utime.tv_sec - start.ru_utime.tv_sec;
  auto useconds = finish.ru_utime.tv_usec - start.ru_utime.tv_usec;
  if (useconds < 0) {
    --seconds;
    useconds += 1000000;
  }
  return static_cast<uint64_t>(seconds) * 1000000ULL +
         static_cast<uint64_t>(useconds);
}

static unsigned parse_node_count(const int argc, const char* argv[]) {
  if (argc < 2) {
    return default_node_count;
  }

  char* end = nullptr;
  errno = 0;
  const unsigned long value = std::strtoul(argv[1], &end, 10);
  if (errno != 0 || end == argv[1] || *end != '\0' ||
      value > std::numeric_limits<unsigned>::max()) {
    std::cerr << "Usage: " << argv[0] << " [node-count]\n";
    std::exit(EXIT_FAILURE);
  }

  return static_cast<unsigned>(value);
}

struct Node {
  Node* next;
  unsigned node_id;
};

static inline Node* create_list(unsigned n) {
  Node* list = nullptr;
  for (unsigned i = 0; i < n; i++)
    list = new Node({list, i});
  return list;
}

static inline void delete_list(Node* list) {
  while (list) {
    Node* node = list;
    list = list->next;
    delete node;
  }
}

static inline void test(unsigned n) {
  struct rusage start, finish;
  get_usage(start);
  delete_list(create_list(n));
  get_usage(finish);

  const uint64_t time_used = user_time_us(start, finish);
  std::cout << "Time used: " << time_used << " usec\n";

  const uint64_t start_rss = max_rss_bytes(start);
  const uint64_t finish_rss = max_rss_bytes(finish);
  const uint64_t mem_used = finish_rss > start_rss ? finish_rss - start_rss : 0;
  std::cout << "Memory used: " << mem_used << " bytes\n";

  const auto mem_required = static_cast<uint64_t>(n) * sizeof(Node);
  std::cout << "Node storage required: " << mem_required << " bytes\n";

  const double overhead =
      mem_used == 0
          ? 0.0
          : (static_cast<double>(mem_used) - static_cast<double>(mem_required)) *
                100.0 / static_cast<double>(mem_used);
  std::cout << "Overhead: " << std::fixed << std::setw(4)
            << std::setprecision(1) << overhead << "%\n";
}

} // namespace

int main(const int argc, const char* argv[]) {
  test(parse_node_count(argc, argv));
  return EXIT_SUCCESS;
}
