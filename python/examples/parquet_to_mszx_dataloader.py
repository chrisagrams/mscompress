"""
Convert a peptide-level parquet to MSZX, then iterate it via a PyTorch
DataLoader with annotations.

What this shows:
  1. parquet -> .mszx using mscompress.parquet.parquet_to_mszx
  2. open the .mszx with MSCompressDataset (PyTorch Dataset)
  3. load Percolator TSV annotations alongside each spectrum
  4. iterate a DataLoader with a custom collate_fn (peak counts vary per
     spectrum, so default stacking won't work)

Run:
  uv run --extra parquet --extra pytorch python examples/parquet_to_mszx_dataloader.py
  # or, with a different parquet:
  uv run --extra parquet --extra pytorch python examples/parquet_to_mszx_dataloader.py /path/to/file.parquet
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

import torch
from torch.utils.data import DataLoader

from mscompress.datasets.torch import MSCompressDataset
from mscompress.parquet import parquet_to_mszx
from mscompress.types import AnnotationFormat


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
# DEFAULT_PARQUET = REPO_ROOT / "test" / "data" / "parquet" / "consensus_wsbin_00001_100rows.parquet"
DEFAULT_PARQUET = REPO_ROOT / "test" / "data" / "parquet" / "consensus_00_100rows.parquet"


def collate_variable_length(batch):
    """DataLoader collate for spectra with differing peak counts.

    Returns three lists, one element per sample:
      - mz tensors (1-D, variable length)
      - intensity tensors (1-D, variable length)
      - annotations dicts (AnnotationFormat -> list[PSM])
    """
    mzs, intensities, anns = [], [], []
    for item in batch:
        if len(item) == 3:
            mz, inten, ann = item
        else:
            mz, inten = item
            ann = {}
        mzs.append(mz)
        intensities.append(inten)
        anns.append(ann)
    return mzs, intensities, anns


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument(
        "parquet",
        nargs="?",
        type=Path,
        default=DEFAULT_PARQUET,
        help=f"Source parquet (default: {DEFAULT_PARQUET})",
    )
    p.add_argument("--batch-size", type=int, default=4)
    p.add_argument("--num-batches", type=int, default=3)
    args = p.parse_args()

    if not args.parquet.exists():
        print(f"error: parquet not found: {args.parquet}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="parquet-mszx-example-") as td:
        mszx_path = Path(td) / (args.parquet.stem + ".mszx")

        # Step 1: parquet -> .mszx (spectra in .msz + Percolator-style TSV)
        print(f"converting {args.parquet.name} -> {mszx_path.name}")
        parquet_to_mszx(
            args.parquet,
            mszx_path,
            # score_column="max_score",
            score_column=None,
            description=f"Converted from {args.parquet.name}",
        )
        print(f"  archive size: {mszx_path.stat().st_size:,} bytes")

        # Step 2: open via the PyTorch Dataset, request Percolator annotations
        dataset = MSCompressDataset(
            mszx_path,
            load_annotations=[AnnotationFormat.PERCOLATOR_TSV],
        )
        print(f"  spectra in dataset: {len(dataset)}")

        # Step 3: DataLoader with a variable-length collate
        loader = DataLoader(
            dataset,
            batch_size=args.batch_size,
            shuffle=False,
            collate_fn=collate_variable_length,
        )

        # Step 4: print the first few batches
        print()
        print(f"iterating {args.num_batches} batch(es) of {args.batch_size}...")
        for batch_idx, (mzs, intensities, anns) in enumerate(loader):
            if batch_idx >= args.num_batches:
                break
            print(f"\nbatch {batch_idx}:")
            for i, (mz, inten, ann) in enumerate(zip(mzs, intensities, anns)):
                psms = ann.get(AnnotationFormat.PERCOLATOR_TSV, [])
                peptide = psms[0].peptide if psms else "<no PSM>"
                charge = psms[0].charge if psms else "?"
                score = f"{psms[0].score:.3f}" if psms else "?"
                print(
                    f"  [{i}] peaks={len(mz):4d}  "
                    f"mz_range=[{mz.min():.4f}, {mz.max():.4f}]  "
                    f"sum_intensity={float(inten.sum()):.3e}  "
                    f"peptide={peptide!r}  charge={charge}  score={score}"
                )

        print()
        print("done.")
    return 0


if __name__ == "__main__":
    main()
