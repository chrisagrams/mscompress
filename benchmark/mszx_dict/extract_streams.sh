#!/usr/bin/env bash
# Compress each subset mzML to .msz (real baseline-i behaviour) and dump its
# raw XML / m/z / intensity streams. Records real .msz sizes.
#
# usage: extract_streams.sh <srcdir> <N> <outdir> [jobs]
set -euo pipefail

SRC="$1"; N="$2"; OUT="$3"; JOBS="${4:-24}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
MSC="$REPO/cli/mscompress"
DICT="$REPO/cli/mszx_dict"

mkdir -p "$OUT/msz" "$OUT/streams"

# Deterministic subset: first N *.mzML sorted by name.
mkdir -p "$OUT"
ls "$SRC"/*.mzML | sort > "$OUT/all_files.txt"
head -n "$N" "$OUT/all_files.txt" > "$OUT/subset.txt"
echo "Selected $(wc -l < "$OUT/subset.txt") files into $OUT/subset.txt"

process_one() {
  idx="$1"; f="$2"
  msz="$OUT/msz/f${idx}.msz"
  "$MSC" "$f" "$msz" >/dev/null 2>&1
  "$DICT" dump "$msz" \
     "$OUT/streams/f${idx}.xml" "$OUT/streams/f${idx}.mz" "$OUT/streams/f${idx}.inten" \
     >/dev/null 2>&1
  printf '%d\t%s\t%d\t%d\n' "$idx" "$f" "$(stat -c %s "$f")" "$(stat -c %s "$msz")" \
     >> "$OUT/msz_sizes.tsv"
}
: > "$OUT/msz_sizes.tsv"
idx=0
active=0
while IFS= read -r f; do
  process_one "$idx" "$f" &
  idx=$((idx+1))
  active=$((active+1))
  if [ "$active" -ge "$JOBS" ]; then wait -n; active=$((active-1)); fi
done < "$OUT/subset.txt"
wait

sort -n -o "$OUT/msz_sizes.tsv" "$OUT/msz_sizes.tsv"
echo "Extracted streams for $(wc -l < "$OUT/msz_sizes.tsv") files"
awk -F'\t' '{m+=$3; z+=$4} END{printf "Total mzML=%.1f MB  total msz(level3)=%.1f MB  ratio=%.4f\n", m/1048576, z/1048576, z/m}' "$OUT/msz_sizes.tsv"
