#!/usr/bin/env bash
# Dictionary-size sweep over extracted streams.
#
# For each stream group (xml, mz, inten) and zstd level, measures:
#   perfile  : sum of per-file-independent frames        (current behavior)
#   pool     : one concatenated frame, default window     (prior winner ref)
#   pool_ldm : one concatenated frame + long-distance matching, large window
#   dict/S   : shared dict of size S trained once + per-file frames using it
#
# Level policy (measured, not assumed): the XML stream is the boilerplate-heavy
# headline and is swept at L19 (primary) and L3. The binary m/z & intensity
# streams are high-entropy doubles: L19 is impractically slow on them (>5 min
# for <0.5 GB) and dict/pool are not expected to help, so they are measured at
# L3 (the tool's actual default) only. This is stated honestly in REPORT.md.
#
# Per-file and dict compressions are parallelized across cores; the (serial,
# large) pool frames run concurrently in the background.
#
# usage: run_sweep.sh <streams_dir> <outdir> [jobs]
set -uo pipefail

STREAMS="$1"; OUT="$2"; JOBS="${3:-96}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
DICT="$REPO/cli/mszx_dict"
mkdir -p "$OUT" "$OUT/dicts" "$OUT/pool"

DICTSIZES="16384 65536 114688 262144 524288 1048576 2097152 4194304"
EXTS="xml mz inten"
CHUNK_KB=16
BUDGET_MB=32
LDM_WINDOWLOG=31   # ~2 GB window so a pooled frame can match across files

levels_for()      { case "$1" in xml) echo "19 3";; *) echo "3";; esac; }
dict_levels_for() { case "$1" in xml) echo "19 3";; *) echo "3";; esac; }
ldms_for()        { echo "0 1"; }   # plain + long-distance-matching pool

CSV="$OUT/results.csv"
echo "stream,level,scheme,maxdict,dict_actual,total_bytes,raw_bytes,train_ms" > "$CSV"

files_for() { ls "$STREAMS"/f*."$1" 2>/dev/null | sort -V; }

# --- launch all pool frames in the background (the long serial pole) -------
POOL_PIDS=""
for ext in $EXTS; do
  for lvl in $(levels_for "$ext"); do
    for ldm in $(ldms_for "$ext"); do
      wl=0; [ "$ldm" = "1" ] && wl=$LDM_WINDOWLOG
      out="$OUT/pool/${ext}_L${lvl}_ldm${ldm}.txt"
      ( $DICT pool "$lvl" "$ldm" "$wl" $(files_for "$ext") > "$out" 2>/dev/null ) &
      POOL_PIDS="$POOL_PIDS $!"
    done
  done
done
echo "launched background pool frames:$POOL_PIDS"

# --- per-file independent (parallel) ---------------------------------------
sum_perfile() { # level ext -> "total raw"
  files_for "$2" | xargs -P "$JOBS" -I{} "$DICT" cfile "$1" - {} \
    | awk -F'\t' '{s+=$1; r+=$2} END{printf "%d %d", s, r}'
}
for ext in $EXTS; do
  for lvl in $(levels_for "$ext"); do
    read tot raw < <(sum_perfile "$lvl" "$ext")
    echo "$ext,$lvl,perfile,0,0,$tot,$raw,0" >> "$CSV"
    echo "  perfile $ext L$lvl: $tot"
  done
done

# --- dict sweep (train all sizes in parallel, then parallel per-file) ------
for ext in $EXTS; do
  raw=$(files_for "$ext" | xargs -P "$JOBS" -I{} stat -c %s {} | awk '{s+=$1} END{print s}')
  for lvl in $(dict_levels_for "$ext"); do
    TRAIN_PIDS=""
    for S in $DICTSIZES; do
      dfile="$OUT/dicts/${ext}_L${lvl}_${S}.dict"
      ( $DICT train "$S" "$CHUNK_KB" "$BUDGET_MB" "$lvl" "$dfile" \
          $(files_for "$ext") > "${dfile}.info" 2>/dev/null ) &
      TRAIN_PIDS="$TRAIN_PIDS $!"
    done
    wait $TRAIN_PIDS
    for S in $DICTSIZES; do
      dfile="$OUT/dicts/${ext}_L${lvl}_${S}.dict"
      tline=$(cat "${dfile}.info" 2>/dev/null)
      dactual=$(echo "$tline" | sed -n 's/.*actual=\([0-9]*\).*/\1/p')
      trainms=$(echo "$tline" | sed -n 's/.*train_ms=\([0-9.]*\).*/\1/p')
      if [ -z "$dactual" ]; then
        echo "$ext,$lvl,dict,$S,NA,NA,$raw,NA" >> "$CSV"
        echo "  dict $ext L$lvl S=$S: TRAIN FAILED"
        continue
      fi
      csum=$(files_for "$ext" | xargs -P "$JOBS" -I{} "$DICT" cfile "$lvl" "$dfile" {} \
             | awk -F'\t' '{s+=$1} END{print s}')
      tot=$((csum + dactual))
      echo "$ext,$lvl,dict,$S,$dactual,$tot,$raw,$trainms" >> "$CSV"
      echo "  dict $ext L$lvl S=$S: actual=$dactual total=$tot train=${trainms}ms"
    done
  done
done

# --- collect pool results --------------------------------------------------
echo "waiting for background pool frames..."
wait $POOL_PIDS
for ext in $EXTS; do
  for lvl in $(levels_for "$ext"); do
    for ldm in $(ldms_for "$ext"); do
      out="$OUT/pool/${ext}_L${lvl}_ldm${ldm}.txt"
      line=$(cat "$out" 2>/dev/null)
      sz=$(echo "$line" | awk -F'\t' '{print $1}')
      raw=$(echo "$line" | awk -F'\t' '{print $2}')
      name="pool"; [ "$ldm" = "1" ] && name="pool_ldm"
      [ -z "$sz" ] && { echo "$ext,$lvl,$name,0,0,NA,NA,0" >> "$CSV"; continue; }
      echo "$ext,$lvl,$name,0,0,$sz,$raw,0" >> "$CSV"
      echo "  $name $ext L$lvl: $sz"
    done
  done
done

echo "done -> $CSV"
