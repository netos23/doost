#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-cmake-build-release}"
node_count="${2:-100000}"
build_dir="${build_dir%/}"

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

baseline_bin="$(resolve_binary pool_baseline)"
pool_bin="$(resolve_binary pool_allocator_benchmark)"

run_benchmark() {
  local binary="$1"
  "$binary" "$node_count"
}

extract_value() {
  local label="$1"
  local text="$2"
  awk -F': ' -v key="$label" '$1 == key { print $2; exit }' <<<"$text"
}

strip_percent() {
  local value="$1"
  printf '%s' "${value%%%}"
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

baseline_output="$(run_benchmark "$baseline_bin")"
pool_output="$(run_benchmark "$pool_bin")"

baseline_time="$(extract_value "Time used" "$baseline_output")"
pool_time="$(extract_value "Time used" "$pool_output")"

baseline_memory="$(extract_value "Memory used" "$baseline_output")"
pool_memory="$(extract_value "Memory used" "$pool_output")"

baseline_required="$(extract_value "Node storage required" "$baseline_output")"
pool_required="$(extract_value "Node storage required" "$pool_output")"

baseline_overhead="$(strip_percent "$(extract_value "Overhead" "$baseline_output")")"
pool_overhead="$(strip_percent "$(extract_value "Overhead" "$pool_output")")"

pool_usable="$(extract_value "Pool usable storage" "$pool_output")"
pool_used="$(extract_value "Pool used storage" "$pool_output")"

cat <<EOF
## Raw results

| Metric | Baseline | Pool |
| --- | ---: | ---: |
| Time used (usec) | ${baseline_time} | ${pool_time} |
| Memory used (bytes) | ${baseline_memory} | ${pool_memory} |
| Node storage required (bytes) | ${baseline_required} | ${pool_required} |
| Overhead (%) | ${baseline_overhead}% | ${pool_overhead}% |
| Pool usable storage (bytes) | - | ${pool_usable} |
| Pool used storage (bytes) | - | ${pool_used} |

## Comparison

| Metric | Baseline | Pool | Delta (Pool - Baseline) | Pool/Baseline |
| --- | ---: | ---: | ---: | ---: |
| Time used (usec) | ${baseline_time} | ${pool_time} | $(format_delta_int "$baseline_time" "$pool_time") | $(format_ratio "$baseline_time" "$pool_time") |
| Memory used (bytes) | ${baseline_memory} | ${pool_memory} | $(format_delta_int "$baseline_memory" "$pool_memory") | $(format_ratio "$baseline_memory" "$pool_memory") |
| Node storage required (bytes) | ${baseline_required} | ${pool_required} | $(format_delta_int "$baseline_required" "$pool_required") | $(format_ratio "$baseline_required" "$pool_required") |
| Overhead (%) | ${baseline_overhead}% | ${pool_overhead}% | $(format_delta_percent "$baseline_overhead" "$pool_overhead") | $(format_ratio "$baseline_overhead" "$pool_overhead") |
EOF
