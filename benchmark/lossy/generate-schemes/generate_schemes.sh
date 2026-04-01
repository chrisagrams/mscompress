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

# Parse algorithm names and targets using only sh + built-in JSON handling.
# We use awk to extract non-experimental algorithms grouped by target.
MZ_ALGOS=$(echo "$JSON" | awk '
  /"name":/   { gsub(/[",]/, "", $2); name=$2 }
  /"target":/  { gsub(/[",]/, "", $2); target=$2 }
  /"experimental":/ {
    gsub(/[",]/, "", $2); exp=$2
    if (exp == "false") {
      if (target == "mz" || target == "mz/int") print name
    }
  }
')

INT_ALGOS=$(echo "$JSON" | awk '
  /"name":/   { gsub(/[",]/, "", $2); name=$2 }
  /"target":/  { gsub(/[",]/, "", $2); target=$2 }
  /"experimental":/ {
    gsub(/[",]/, "", $2); exp=$2
    if (exp == "false") {
      if (target == "int" || target == "mz/int") print name
    }
  }
')

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
