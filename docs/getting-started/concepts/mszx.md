# MSZX archives

MSZX is a **tar archive** that bundles:

- An MSZ-compressed spectra file
- One or more search-result (PSM) files, stored as **mzTab**. pepXML and
  Percolator TSV/PIN output is converted to mzTab when the archive is written;
  the original file is not kept
- A JSON `manifest.json` describing what's in the archive

The point is to ship a single, self-describing artifact for downstream ML
training or analysis: spectra + identifications + the `join_key` that links
them.

Manifest fields:

| Field | Purpose |
|-------|---------|
| `version` | MSZX format version |
| `created_at` | Build timestamp |
| `spectra_file` | Path to the MSZ inside the archive |
| `num_spectra` | Spectrum count |
| `annotations` | List of `{filename, format, compressed}` entries (`format` is `mztab`) |
| `join_key` | Column to join annotations against spectra (`spectra_ref` for new archives; `scan_number` in older ones) |
| `description` | Free-text description |
| `extra` | Open dictionary for custom fields |

## Two layouts

The table above describes a **v1** archive: exactly one spectra file, with
archive-level annotations. You build it with `MSZXBuilder` and read it with
`MSZXFile`.

A **v2 ("batch")** archive bundles N spectra files instead — a whole cohort in
one file — with annotations scoped per entry. You build it with the CLI's
`--batch`, `compress_batch()` (Python), or `compressBatch()` (Node), and read it
with `MSZXBatchFile`. Batch archives carry no timestamps, so the same inputs
always produce byte-identical output.

`read()` picks the right reader from the manifest, so you rarely choose
explicitly.

See:

- [Python MSZX guide](../../python/guides/mszx.md)
- [Node.js MSZX guide](../../node/guides/mszx.md)
- [MSZX format spec](../../format/mszx.md)

## Older archives

Archives written before mzTab became the only stored format carry the raw
pepXML / Percolator files. They are still readable through the same APIs.
