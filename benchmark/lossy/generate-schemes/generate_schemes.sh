#!/bin/sh
# Dynamically generate lossy compression scheme combinations from the
# algorithm registry reported by `mscompress --list-algorithms --json`.
#
# Output: /results/schemes.txt — one line per scheme in the format:
#   label:mz_algo:int_algo
#
# Always includes a "lossless" baseline (no lossy flags).
# For each algorithm, creates a single-target scheme.
# For each (mz_algo, int_algo) pair, creates a combined scheme.
# Skips experimental algorithms.
set -eu

SCHEMES_FILE="/results/schemes.txt"
mkdir -p /results

JSON=$(mscompress --list-algorithms --json)

# Parse algorithm names and targets using jq.
MZ_ALGOS=$(echo "$JSON" | jq -r '.[] | select(.experimental == false) | select(.target == "mz" or .target == "mz/int") | .name')

INT_ALGOS=$(echo "$JSON" | jq -r '.[] | select(.experimental == false) | select(.target == "int" or .target == "mz/int") | .name')

# Start with lossless baseline
echo "lossless::" > "$SCHEMES_FILE"

# Single m/z algorithms
for mz in $MZ_ALGOS; do
  echo "mz_${mz}:${mz}:" >> "$SCHEMES_FILE"
done

# Single intensity algorithms
for int_algo in $INT_ALGOS; do
  echo "int_${int_algo}::${int_algo}" >> "$SCHEMES_FILE"
done

# Combined schemes (every mz x int combination)
for mz in $MZ_ALGOS; do
  for int_algo in $INT_ALGOS; do
    echo "mz_${mz}_int_${int_algo}:${mz}:${int_algo}" >> "$SCHEMES_FILE"
  done
done

echo "Generated $(wc -l < "$SCHEMES_FILE") schemes:"
cat "$SCHEMES_FILE"
