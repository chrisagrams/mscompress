#!/bin/bash
# Run MSFragger + Percolator on each scheme.
# Expects /results/schemes.txt from the generate-schemes step.
set -euo pipefail

SCHEMES_FILE="/results/schemes.txt"
ORIGINAL_MZML="/data/reference.mzML"
FASTA_SRC="/data/UP000005640_9606.fasta"
FASTA_DIR="/results/db"
FRAGPIPE_HOME="/fragpipe_bin/fragpipe-24.0/fragpipe-24.0"
PERCOLATOR="${PERCOLATOR:-$(find "$FRAGPIPE_HOME/tools" -name 'percolator' -type f 2>/dev/null | head -1)}"
FRAGGER_JAR="${FRAGGER_JAR:-$(find "$FRAGPIPE_HOME/tools" /ext -name 'MSFragger-*.jar' 2>/dev/null | head -1)}"
if [ -z "$FRAGGER_JAR" ]; then
  echo "ERROR: MSFragger JAR not found. Mount it to /ext/ or set FRAGGER_JAR." >&2
  echo "MSFragger requires an academic license: https://msfragger.nesvilab.org/" >&2
  exit 1
fi

# Copy FASTA to writable location and append reversed decoy sequences.
# MSFragger + Percolator require a target-decoy database for FDR estimation.
mkdir -p "$FASTA_DIR"
FASTA="$FASTA_DIR/$(basename "$FASTA_SRC")"
if [ ! -f "$FASTA" ]; then
  cp "$FASTA_SRC" "$FASTA"
  # Generate reversed decoy entries with rev_ prefix
  awk '
    /^>/ {
      if (header) print header "\n" seq
      header = ">rev_" substr($0, 2)
      seq = ""
      next
    }
    { seq = seq $0 }
    END { if (header) print header "\n" seq }
  ' "$FASTA_SRC" | awk '{
    if (/^>/) { print; next }
    # Reverse the sequence
    n = split($0, a, "")
    s = ""
    for (i = n; i >= 1; i--) s = s a[i]
    print s
  }' >> "$FASTA"
fi

run_search() {
  local label="$1"
  local mzml_path="$2"
  local out_dir="/results/${label}"

  echo "--- Searching: $label ---"
  mkdir -p "$out_dir"

  # MSFragger writes output files next to the input mzML.
  # Symlink the mzML into the output dir if it isn't already there.
  local mzml_basename
  mzml_basename=$(basename "$mzml_path")
  local mzml_in_outdir="$out_dir/$mzml_basename"
  if [ "$(realpath "$mzml_path")" != "$(realpath "$mzml_in_outdir" 2>/dev/null)" ]; then
    ln -sf "$(realpath "$mzml_path")" "$mzml_in_outdir"
  fi

  (cd "$out_dir" && java -Xmx16G -jar "$FRAGGER_JAR" \
    /config/closed_fragger.params \
    "$mzml_in_outdir")

  # Percolator rescores PSMs from the .pin file MSFragger produces.
  PIN_FILE=$(find "$out_dir" -name '*.pin' | head -1)
  if [ -z "$PIN_FILE" ]; then
    echo "ERROR: No .pin file found in $out_dir" >&2
    exit 1
  fi

  "$PERCOLATOR" \
    --results-psms "$out_dir/percolator-psms.txt" \
    --decoy-results-psms "$out_dir/percolator-decoy-psms.txt" \
    --only-psms \
    --post-processing-tdc \
    "$PIN_FILE"
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
