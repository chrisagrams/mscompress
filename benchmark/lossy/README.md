# Lossy Compression Benchmark

Evaluates how mscompress lossy compression algorithms affect downstream peptide identification quality. Compresses a reference mzML with each lossy scheme, decompresses back to mzML, runs MSFragger + Percolator on each, and compares peptide IDs against the uncompressed baseline.

## Prerequisites

- Docker and Docker Compose
- ~10 GB free disk space
- Internet access (first run downloads reference data)

> **Licensing:** MSFragger is free for academic and non-commercial use. Commercial users must obtain a license from [Fragmatics](https://www.fragmatics.com/).

## Quick Start

```bash
cd benchmark/lossy
docker compose up
```

The full pipeline runs as a sequence of dependent Docker Compose services. Each step waits for the previous step to complete before starting.

## Project Structure

Each pipeline step lives in its own subdirectory with a `Dockerfile` and associated scripts:

```
benchmark/lossy/
  docker-compose.yml          # Orchestrates the full pipeline
  download/
    Dockerfile                # Alpine + curl
    download.sh               # Downloads reference mzML + FASTA
  generate-schemes/
    Dockerfile                # Builds mscompress into Alpine
    generate_schemes.sh       # Queries algorithm registry, writes schemes.txt
  compress/
    Dockerfile                # Builds mscompress into Alpine
    compress.sh               # Compresses + decompresses each scheme
  search/
    Dockerfile                # Extends fcyucn/fragpipe
    search.sh                 # MSFragger + Percolator + Philosopher
    closed_fragger.params     # MSFragger search parameters
  analysis/
    Dockerfile                # Python + matplotlib/pandas
    analyze.py                # Compares peptide IDs, produces plots
```

## Pipeline

| Service | Directory | Description |
|---------|-----------|-------------|
| `download` | `download/` | Downloads reference mzML and UniProt human proteome FASTA to `data/` |
| `generate-schemes` | `generate-schemes/` | Queries `mscompress --list-algorithms --json` to dynamically build all lossy scheme combinations |
| `compress` | `compress/` | Compresses the reference mzML with each scheme, then decompresses back to mzML |
| `search` | `search/` | Runs MSFragger database search + Percolator PSM rescoring + Philosopher FDR filtering on the original and each decompressed mzML |
| `analysis` | `analysis/` | Compares peptide identifications and produces a bar chart + summary TSV |

### Dependency chain

```
download -> generate-schemes -> compress -> search -> analysis
```

## Dynamic Scheme Generation

Lossy schemes are **not hardcoded**. The `generate-schemes` service runs `mscompress --list-algorithms --json` and automatically generates:

- A **lossless** baseline (no lossy flags)
- A single-target scheme for each algorithm (m/z-only or intensity-only)
- Combined schemes for every (m/z algorithm x intensity algorithm) pair
- Experimental algorithms are excluded

The generated scheme list is written to `results/schemes.txt` and consumed by all downstream steps.

## Output

Results are written to `results/`:

```
results/
  schemes.txt              # Generated scheme definitions
  original/                # MSFragger search on uncompressed mzML
  lossless/                # Each scheme directory contains:
    compressed.msz         #   Compressed file
    decompressed.mzML      #   Decompressed mzML
    psm.tsv                #   FDR-filtered peptide-spectrum matches
  mz_cast/
  ...
  plots/
    peptide_comparison.png # Diverging bar chart
    summary.tsv            # Tabular summary
```

### Reading the Plot

- **Green bars (positive y):** Peptides gained -- identified in the lossy-compressed mzML but not the original.
- **Red bars (negative y):** Peptides lost -- identified in the original but not the lossy-compressed mzML.
- **Numbers above bars:** Total unique peptides for that scheme.
- The `lossless` scheme should show zero gained/lost (identical to baseline).

## Customization

### Use a different mzML

Edit `download/download.sh` to change the `MZML_URL`, or place your own file at `data/reference.mzML`.

### Change search parameters

Edit `search/closed_fragger.params` to adjust MSFragger settings (tolerances, enzyme, modifications, etc.).

### Run a single service

```bash
docker compose up download          # Just download data
docker compose up generate-schemes  # Generate schemes (runs download first)
docker compose up compress          # Compress all schemes (runs prior steps)
docker compose up search            # Run searches (runs prior steps)
docker compose up analysis          # Analyze results (runs all steps)
```

## Troubleshooting

- **Disk space:** Each decompressed mzML is the same size as the original. Expect N x original file size in `results/`, where N is the number of generated schemes.
- **Memory:** MSFragger uses up to 16 GB heap by default. Adjust `-Xmx` in `search/search.sh` if needed.
- **Docker permissions:** Output files may be owned by root. Mount volumes or adjust as needed.
- **Download failures:** Delete the partial file from `data/` and re-run.
