#!/bin/bash
# Run MSFragger + Percolator + Philosopher on each scheme.
# Expects /results/schemes.txt from the generate-schemes step.
set -euo pipefail

SCHEMES_FILE="/results/schemes.txt"
ORIGINAL_MZML="/data/reference.mzML"
FRAGGER_JAR=$(find /tools -name 'MSFragger-*.jar' | head -1)

run_search() {
  local label="$1"
  local mzml_path="$2"
  local out_dir="/results/${label}"

  echo "--- Searching: $label ---"
  cd "$out_dir"

  philosopher workspace --init --nocheck
  philosopher database --annotate /data/UP000005640_9606.fasta

  java -Xmx16G -jar "$FRAGGER_JAR" /config/closed_fragger.params "$mzml_path"

  # Use Percolator for PSM rescoring instead of PeptideProphet.
  # MSFragger produces .pin files that Percolator consumes directly.
  PIN_FILE=$(find "$out_dir" -name '*.pin' | head -1)
  if [ -z "$PIN_FILE" ]; then
    echo "ERROR: No .pin file found in $out_dir" >&2
    exit 1
  fi

  percolator \
    --results-psms "$out_dir/percolator-psms.txt" \
    --decoy-results-psms "$out_dir/percolator-decoy-psms.txt" \
    --only-psms \
    --post-processing-tdc \
    "$PIN_FILE"

  philosopher filter \
    --percolator "$out_dir/percolator-psms.txt" \
    --tag rev_ \
    --psm 0.01 --pep 0.01 --prot 1.0 \
    --sequential --razor

  philosopher report
  philosopher workspace --clean
}

# Search original (baseline)
mkdir -p "/results/original"
run_search "original" "$ORIGINAL_MZML"

# Search each scheme
if [ ! -f "$SCHEMES_FILE" ]; then
  echo "ERROR: $SCHEMES_FILE not found." >&2
  exit 1
fi

while IFS=: read -r label _ _; do
  run_search "$label" "/results/${label}/decompressed.mzML"
done < "$SCHEMES_FILE"

echo "All searches complete."
