#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-cmake-build-release}"
node_count="${2:-10000000}"
thread_count="${3:-16}"
build_dir="${build_dir%/}"

benchmarks=(
  "standard-new-delete:standard_allocator_benchmark"
  "global-mutex-pool:global_mutex_pool_benchmark"
  "global-nonblocking-pool:global_nonblocking_pool_benchmark"
  "thread-local-pools:thread_local_pool_benchmark"
)

resolve_binary() {
  local relative_path="$1"
  local candidate

  for candidate in \
    "${build_dir}/${relative_path}" \
    "${build_dir}/build/${relative_path}"
  do
    if [[ -x "$candidate" ]]; then
      printf '%s' "$candidate"
      return 0
    fi
  done

  printf 'Missing benchmark binary: %s/%s (also checked %s/build/%s)\n' \
    "$build_dir" "$relative_path" "$build_dir" "$relative_path" >&2
  return 1
}

extract_number() {
  local label="$1"
  local text="$2"
  awk -F': ' -v key="$label" '$1 == key { sub(/^[[:space:]]+/, "", $2); gsub(/[^0-9.-].*/, "", $2); print $2; exit }' <<<"$text"
}

format_delta_int() {
  awk -v baseline="$1" -v value="$2" 'BEGIN { printf("%+.0f", value - baseline) }'
}

format_delta_percent() {
  awk -v baseline="$1" -v value="$2" 'BEGIN { printf("%+.1f%%", value - baseline) }'
}

format_ratio() {
  awk -v baseline="$1" -v value="$2" 'BEGIN { if (baseline <= 0) { print "n/a" } else { printf("%.2fx", value / baseline) } }'
}

names=()
times=()
cpu_times=()
memories=()
required=()
overheads=()
pool_usable=()
pool_used=()
raw_outputs=()

for entry in "${benchmarks[@]}"; do
  name="${entry%%:*}"
  binary_name="${entry#*:}"
  binary="$(resolve_binary "$binary_name")"
  output="$("$binary" "$node_count" "$thread_count")"

  names+=("$name")
  times+=("$(extract_number "Time used" "$output")")
  cpu_times+=("$(extract_number "CPU time used" "$output")")
  memories+=("$(extract_number "Memory used" "$output")")
  required+=("$(extract_number "Node storage required" "$output")")
  overheads+=("$(extract_number "Overhead" "$output")")
  pool_usable+=("$(extract_number "Pool usable storage" "$output")")
  pool_used+=("$(extract_number "Pool used storage" "$output")")
  raw_outputs+=("$output")
done

cat <<EOF
## Benchmark parameters

- Node count per thread: ${node_count}
- Threads: ${thread_count}

## Raw results

| Allocator | Wall time (usec) | CPU time (usec) | Memory used (bytes) | Node storage required (bytes) | Pool usable storage (bytes) | Pool used storage (bytes) | Overhead |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
EOF

for index in "${!names[@]}"; do
  printf '| %s | %s | %s | %s | %s | %s | %s | %s%% |\n' \
    "${names[$index]}" \
    "${times[$index]}" \
    "${cpu_times[$index]}" \
    "${memories[$index]}" \
    "${required[$index]}" \
    "${pool_usable[$index]}" \
    "${pool_used[$index]}" \
    "${overheads[$index]}"
done

cat <<EOF

## Comparison Against Standard Allocator

| Allocator | Wall delta | Wall ratio | Memory delta | Memory ratio | Overhead delta |
| --- | ---: | ---: | ---: | ---: | ---: |
EOF

baseline_time="${times[0]}"
baseline_memory="${memories[0]}"
baseline_overhead="${overheads[0]}"

for index in "${!names[@]}"; do
  printf '| %s | %s | %s | %s | %s | %s |\n' \
    "${names[$index]}" \
    "$(format_delta_int "$baseline_time" "${times[$index]}")" \
    "$(format_ratio "$baseline_time" "${times[$index]}")" \
    "$(format_delta_int "$baseline_memory" "${memories[$index]}")" \
    "$(format_ratio "$baseline_memory" "${memories[$index]}")" \
    "$(format_delta_percent "$baseline_overhead" "${overheads[$index]}")"
done

cat <<EOF

## Raw Program Output
EOF

for index in "${!names[@]}"; do
  printf '\n### %s\n\n```text\n%s\n```\n' "${names[$index]}" "${raw_outputs[$index]}"
done
