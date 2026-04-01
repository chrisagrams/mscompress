#!/bin/sh
set -eu

DATA_DIR="/data"

MZML_URL="https://uofi.box.com/shared/static/74pf1i9wug1hz9xmsnjlvrcr3wkx1nn8?dl=1"
FASTA_URL="https://ftp.uniprot.org/pub/databases/uniprot/current_release/knowledgebase/reference_proteomes/Eukaryota/UP000005640/UP000005640_9606.fasta.gz"

mkdir -p "$DATA_DIR"

# Download reference mzML
if [ -f "$DATA_DIR/reference.mzML" ] && [ -s "$DATA_DIR/reference.mzML" ]; then
    echo "reference.mzML already exists, skipping download."
else
    echo "Downloading reference mzML..."
    curl -L -o "$DATA_DIR/reference.mzML" "$MZML_URL"
fi

# Download UniProt human FASTA
if [ -f "$DATA_DIR/UP000005640_9606.fasta" ] && [ -s "$DATA_DIR/UP000005640_9606.fasta" ]; then
    echo "UP000005640_9606.fasta already exists, skipping download."
else
    echo "Downloading UniProt human proteome FASTA..."
    curl -L -o "$DATA_DIR/UP000005640_9606.fasta.gz" "$FASTA_URL"
    echo "Decompressing FASTA..."
    gunzip "$DATA_DIR/UP000005640_9606.fasta.gz"
fi

# Validate
for f in "$DATA_DIR/reference.mzML" "$DATA_DIR/UP000005640_9606.fasta"; do
    if [ ! -s "$f" ]; then
        echo "ERROR: $f is missing or empty." >&2
        exit 1
    fi
    echo "OK: $f ($(du -h "$f" | cut -f1))"
done

echo "All reference data ready."
