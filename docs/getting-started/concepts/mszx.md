# MSZX archives

MSZX is a **tar archive** that bundles:

- An MSZ-compressed spectra file
- One or more annotation files (pepXML, Percolator `.pin` TSV, or anything
  else identifying peptides/proteins per scan)
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
| `annotations` | List of `{name, file, format}` entries |
| `join_key` | Column to join annotations against spectra (typically `scan_number`) |
| `description` | Free-text description |
| `extra` | Open dictionary for custom fields |

You build MSZX archives with `MSZXBuilder` (Python or TypeScript) and read
them with `MSZXFile`. See:

- [Python MSZX guide](../../python/guides/mszx.md)
- [Node.js MSZX guide](../../node/guides/mszx.md)
- [MSZX format spec](../../format/mszx.md)
