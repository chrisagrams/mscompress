#!/bin/sh
# Compress and decompress reference mzML for each scheme in /results/schemes.txt.
set -eu

SCHEMES_FILE="/results/schemes.txt"
ORIGINAL_MZML="/data/reference.mzML"

if [ ! -f "$SCHEMES_FILE" ]; then
  echo "ERROR: $SCHEMES_FILE not found. Run generate-schemes first." >&2
  exit 1
fi

while IFS=: read -r label mz_algo int_algo; do
  echo "--- $label ---"
  mkdir -p "/results/${label}"

  COMPRESS_ARGS=""
  [ -n "$mz_algo" ] && COMPRESS_ARGS="$COMPRESS_ARGS --mz-lossy $mz_algo"
  [ -n "$int_algo" ] && COMPRESS_ARGS="$COMPRESS_ARGS --int-lossy $int_algo"

  # Compress
  # shellcheck disable=SC2086
  mscompress $COMPRESS_ARGS "$ORIGINAL_MZML" "/results/${label}/compressed.msz"

  # Decompress
  mscompress "/results/${label}/compressed.msz" "/results/${label}/decompressed.mzML"
done < "$SCHEMES_FILE"

echo "All schemes compressed and decompressed."
