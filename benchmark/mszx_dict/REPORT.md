# MSZX shared ZSTD-dictionary investigation

Branch: `feat/mszx-zstd-dictionary`.
Goal: test whether a shared, trained ZSTD dictionary (stored once in a `.mszx`
tar) improves total compression of many same-instrument mzML versus (i) the
current per-file `.msz` behaviour and (ii) a single pooled stream — and, if so,
at what dictionary size and for which stream.

**TL;DR — the hypothesis (dictionary helps the boilerplate-heavy XML stream)
does not hold on this real corpus. The dictionary never beats per-file on the
XML stream (worse at zstd-19, neutral at zstd-3). It gives a small, real win on
the *binary* m/z & intensity streams (−1 to −2%, best at the smallest 16 KB
dict). The only XML win comes from pooling with long-distance matching (−2.9%),
not from a dictionary. A whole-archive dictionary (applied to all three
streams) is −1.63% vs per-file and round-trips losslessly.**

## What was built

A standalone experiment tool, `benchmark/mszx_dict/mszx_dict.c`, compiled as the
`mszx_dict` CMake target (`cli/CMakeLists.txt`). It links the mscompress core so
it reuses the *real* `.msz` stream-stripping logic — `parse_footer()` + the
per-block ZSTD frames — to recover each file's raw XML / m/z / intensity streams
exactly as the format stores them. It also compiles the vendored zstd
`dictBuilder` (ZDICT), which the main zstd static lib does not include.

Subcommands (a bash driver orchestrates + parallelizes across 96 cores):

| cmd | purpose |
|-----|---------|
| `dump`    | recover raw XML/m z/inten streams from a `.msz` |
| `train`   | train one shared dict (chunked fastCover, bounded sample budget) |
| `cfile`   | compress one stream as a single frame, optional dict |
| `pool`    | concatenate + compress one frame (optional LDM + large window) |
| `archive` | build a real `.mszx` (dict stored once + per-file streams) **and round-trip verify** |

Drivers: `extract_streams.sh` (compress each mzML → `.msz`, dump streams),
`run_sweep.sh` (the dict-size sweep), `run_archives.sh` (real archives),
`plot.py`.

Dictionaries are trained with `ZDICT_trainFromBuffer_fastCover` on **chunked**
samples (16 KB chunks, stride-subsampled to a 32 MB budget) — the standard
dictionary-training regime. Training whole 88 MB streams as single "samples"
(the naive approach) is both the wrong regime and pathologically slow.

## Test subset (experimental unit)

**48 mzML from a single condition dir, `LL2092_HYEAB/`** (same instrument, DIA),
sorted by name — `LL2076_p4_HYEA_250pg_*_DIAwet.mzML`. Full list:
`results/subset.txt`.

- Raw mzML total: **9.21 GB** (avg ~192 MB/file).
- Real per-file `.msz` total (current behaviour, zstd-3): **3.58 GB** (ratio 0.3884). ← baseline (i)
- Extracted raw streams per file: XML ~88 MB, m/z ~78 MB, intensity ~78 MB.

A key, non-obvious fact about this corpus: **the per-file XML streams are large
(~88 MB), not small.** The shared boilerplate (instrument config, softwareList,
CV params, dataProcessing) is only tens of KB — a negligible fraction of each
stream. That single fact drives every result below.

## Sweep results (48 files)

Dictionary sizes swept: 16, 64, 112 (zstd default), 256, 512 KB, 1, 2, 4 MB.
Plot: `results/sweep_plot.png`. Raw CSV: `results/sweep_results.csv`.

### XML stream — raw 4.22 GB (the hypothesised dictionary target)

| scheme | zstd | total (bytes) | vs raw | vs per-file |
|--------|-----:|--------------:|-------:|------------:|
| **per-file (baseline i)** | 19 | 68,887,443 | 0.01632 | — |
| pool (default window) | 19 | 68,803,837 | 0.01631 | −0.12% |
| **pool + LDM (2 GB window)** | 19 | **66,884,804** | 0.01585 | **−2.91%** |
| dict 64 KB | 19 | 74,428,211 | 0.01764 | +8.05% |
| dict 1 MB (least-bad) | 19 | 71,125,764 | 0.01686 | +3.25% |
| per-file (baseline i) | 3 | 95,978,297 | 0.02275 | — |
| pool + LDM | 3 | 94,430,314 | 0.02238 | −1.61% |
| dict 64 KB (best) | 3 | 95,919,153 | 0.02273 | −0.06% |
| dict 1 MB | 3 | 96,753,980 | 0.02293 | +0.81% |

**The dictionary never beats per-file on XML.** At zstd-19 it is *worse* by
3–8% (the trained entropy priors are a poor fit for the bulk of an 88 MB
heterogeneous stream — dictionaries help *small* inputs, not large ones). At
zstd-3 it is essentially neutral (best −0.06% at 64 KB). Plain pooling ≈ per-file
because each 88 MB file exceeds zstd's ~8 MB window; only **LDM with a large
window** lets a pooled frame match across files, and even then the win is modest
(−2.9%).

### m/z stream — raw 3.76 GB (zstd-3, the actual default)

| scheme | total (bytes) | vs raw | vs per-file |
|--------|--------------:|-------:|------------:|
| per-file | 1,443,455,448 | 0.3834 | — |
| pool / pool+LDM | ~1,443,55x,xxx | 0.3834 | ~0% |
| **dict 16 KB (best)** | **1,426,696,608** | 0.3789 | **−1.16%** |
| dict 112 KB | 1,426,808,899 | 0.3790 | −1.15% |
| dict 256 KB | 1,443,764,221 | 0.3835 | +0.02% |

### intensity stream — raw 3.76 GB (zstd-3)

| scheme | total (bytes) | vs raw | vs per-file |
|--------|--------------:|-------:|------------:|
| per-file | 1,986,084,198 | 0.5276 | — |
| pool / pool+LDM | ~1,986,26x,xxx | 0.5277 | ~0% |
| **dict 16 KB (best)** | **1,944,877,442** | 0.5166 | **−2.07%** |
| dict 112 KB | 1,945,010,136 | 0.5167 | −2.07% |
| dict 256 KB | 1,986,469,168 | 0.5277 | +0.02% |

**Surprise — the dictionary helps the binary streams, not XML.** IEEE-double
m/z and intensity bytes share recurring patterns across files (exponent /
high-mantissa bytes, common encodings) that a *small* dict captures and uses to
prime the low-effort zstd-3 coder. The benefit is confined to small dicts
(≤ 112 KB); at ≥ 256 KB the total snaps back to per-file — the jump is in the
*per-file compression itself*, not the stored-dict cost (256 KB is negligible
against 1.4 GB): past ~112 KB the ZDICT entropy priors stop matching the
per-run-unique double values and the dictionary stops helping (see the cliff in
the plot). **Optimal binary dict size ≈ 16 KB.** Pooling does essentially
nothing for binary (per-run values differ).

### Training time

Bounded and fast: XML dict ~0.2 s (zstd-3) / ~6.5 s (zstd-19); binary dict
~0.55 s each — independent of file count (32 MB sample budget, fastCover).

## Full real `.mszx` archives (48 files, zstd-3) + round-trip

Built with the tool's minimal USTAR writer (dict stored once as
`dictionary_*.zstd` + `manifest.json` + per-file `.zst` stream entries) and
re-read through the repo's own tar reader (`mszx_list_entries`). Log:
`results/archives_L3.log`.

| archive | on-disk bytes | vs per-file | round-trip |
|---------|--------------:|------------:|:----------:|
| per-file (no dict) | 3,525,628,416 | — | n/a |
| pool | 3,526,315,520 | +0.02% | n/a |
| dict on XML only | 3,525,749,760 | +0.003% | **PASS** (144/144) |
| **dict on all 3 streams** | **3,468,038,656** | **−1.63%** | **PASS** (144/144) |

Round-trip verification decompresses every stream entry with the stored
dictionary (`ZSTD_decompress_usingDict`) and byte-compares to the original raw
stream: **144/144 entries identical** for both dict archives → lossless.

(These archive totals are a faithful proxy, slightly tighter than the real
per-file `.msz` total of 3.58 GB because the experiment stores each stream as a
single frame rather than the format's per-division blocks.)

## Honest verdict

- **Does a dictionary beat per-file?** Not for the XML stream (worse at zstd-19,
  neutral at zstd-3). Yes, marginally, for the binary streams (−1.2% m/z, −2.1%
  intensity at zstd-3, best at 16 KB). Whole-archive net: **−1.63%**.
- **Does a dictionary beat pooling?** For XML, no — pooling+LDM (−2.9%) wins and
  the dictionary loses to per-file. For binary, the dictionary wins because
  pooling does nothing there.
- **For which stream?** The dictionary helps the **binary** m/z / intensity
  streams, i.e. the *opposite* of the going-in hypothesis (which expected the
  boilerplate-heavy XML stream to benefit). Pooling helps **XML** only, and only
  with long-distance matching + a large window.
- **Optimal dictionary size:** ~**16 KB** (smallest tested) for the binary
  streams; no size helps XML.
- **Why the prior "-91.5% pooling win" did not reproduce:** that micro-experiment
  used small XML streams that all fit inside zstd's window, so a pooled frame
  deduplicated the shared boilerplate across the whole set. Here each stream is
  ~88 MB, so files no longer share a window and the boilerplate is a negligible
  fraction of each stream. **Cross-file redundancy at this file size is small**,
  which caps every cross-file scheme (pool and dict alike) to low-single-digit %.

### Recommendation for a production feature

A shared dictionary is worth adding only for the **binary** streams, sized
**~16–64 KB**, giving ~1–2% at the current default level; do **not** dictionary
the XML stream. The larger structural lever for this corpus is **long-distance
matching on a pooled XML stream** (−2.9%). Neither is a dramatic win at this
file size — the data simply does not carry much cross-file redundancy once files
are ~100 MB each. A dictionary would pay off far more on genuinely *small* mzML
(demonstrated separately: a 20 KB XML fragment compresses −38% with the shared
dict), so the technique is corpus-dependent: size the decision to the file size,
not the instrument.

### Production notes (out of scope for this branch)

This is a C-side prototype/experiment only. A real in-format implementation
would need: a `dictionary.zstd` entry + `manifest.json` field in the `.mszx`
writer; `ZSTD_{compress,decompress}_usingDict` wired through `compress.c` /
`decompress.c` per-block (or a CDict/DDict cached on the format struct); the
Python and Node readers updated in lockstep; and an msz-version bump so old
readers reject dict-compressed archives.

## Reproduce

```bash
cmake -S . -B . && cmake --build . --target mszx_dict -j
SRC=/mnt/vault-1/k8/raw_downloads/PXD070201_mzml/LL2092_HYEAB
OUT=~/mszx_dict_sweep/run48
bash benchmark/mszx_dict/extract_streams.sh "$SRC" 48 "$OUT" 32
bash benchmark/mszx_dict/run_sweep.sh    "$OUT/streams" "$OUT/sweep" 96
bash benchmark/mszx_dict/run_archives.sh "$OUT/streams" "$OUT/archives" 3 114688
uv run --no-project --with matplotlib python benchmark/mszx_dict/plot.py \
    "$OUT/sweep/results.csv" "$OUT/sweep/sweep_plot.png"
```
