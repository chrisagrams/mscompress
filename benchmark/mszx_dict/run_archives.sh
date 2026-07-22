#!/usr/bin/env bash
# Build real .mszx archives for the subset under each scheme and record real
# on-disk sizes. Dict modes additionally round-trip verify (decompress every
# stream with the stored dictionary and byte-compare to the original).
#
# usage: run_archives.sh <streams_dir> <outdir> <level> [maxdict]
set -uo pipefail

STREAMS="$1"; OUT="$2"; LEVEL="${3:-3}"; MAXDICT="${4:-114688}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DICT="$REPO/cli/mszx_dict"
mkdir -p "$OUT"

MAN="$OUT/manifest.tsv"
: > "$MAN"
for x in $(ls "$STREAMS"/f*.xml | sort -V); do
  i=$(basename "$x" .xml)
  printf '%s\t%s\t%s\n' "$STREAMS/$i.xml" "$STREAMS/$i.mz" "$STREAMS/$i.inten" >> "$MAN"
done
echo "manifest: $(wc -l < "$MAN") files, level=$LEVEL maxdict=$MAXDICT"

LOG="$OUT/archives_L${LEVEL}.log"
: > "$LOG"
for mode in perfile pool dictxml dictall; do
  md=0; [ "$mode" = dictxml -o "$mode" = dictall ] && md=$MAXDICT
  echo ">>> $mode" | tee -a "$LOG"
  "$DICT" archive "$OUT/arch_${mode}_L${LEVEL}.mszx" "$LEVEL" "$mode" "$md" "$MAN" \
    2>&1 | tee -a "$LOG"
done
echo "done -> $LOG"
