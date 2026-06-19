# MSZX format

MSZX is a **tar archive** bundling an MSZ-compressed spectra file with
annotation files and a JSON manifest. It's a transport format — what you
ship when you want to give someone "the data plus the identifications."

## Archive layout

```
archive.mszx                 (uncompressed tar)
├── manifest.json
├── spectra.msz              (or whatever the manifest names it)
└── annotations/
    ├── psm.pin              (Percolator TSV)
    ├── search.pep.xml       (pepXML)
    └── ...                  (more annotation files)
```

The exact filenames are not fixed — `manifest.json` records the path of
each file inside the archive.

## manifest.json

```json
{
  "version": 1,
  "created_at": "2026-05-11T14:30:00Z",
  "spectra_file": "spectra.msz",
  "num_spectra": 12453,
  "join_key": "scan_number",
  "description": "DDA proteomics run #42",
  "annotations": [
    {
      "name": "percolator",
      "file": "annotations/psm.pin",
      "format": "percolator_pin"
    },
    {
      "name": "comet",
      "file": "annotations/search.pep.xml",
      "format": "pepxml"
    }
  ],
  "extra": {
    "instrument": "Orbitrap Eclipse",
    "study_id": "PXD012345"
  }
}
```

### Fields

| Field | Type | Required | Purpose |
|-------|------|----------|---------|
| `version` | int | yes | MSZX manifest version |
| `created_at` | ISO-8601 timestamp | yes | Build time |
| `spectra_file` | string | yes | Path to the MSZ inside the archive |
| `num_spectra` | int | yes | Spectrum count in the MSZ |
| `join_key` | string | yes | Column name to join annotations against spectra (typically `scan_number`) |
| `description` | string | no | Free-text description |
| `annotations` | array | no | Annotation entries — `{name, file, format}` |
| `extra` | object | no | Open dictionary for custom metadata |

### Annotation entry

| Field | Type | Purpose |
|-------|------|---------|
| `name` | string | Logical name (must be unique within the archive) |
| `file` | string | Path inside the archive |
| `format` | string | `percolator_pin`, `pepxml`, or another reader-supported format |

## Annotation file formats

Annotations are read by the bindings' annotation readers (see Python's
`mscompress.annotations` and Node's MSZX archive reader). Files may be
zstd-compressed on disk; the readers handle transparent decompression.

## Building MSZX archives

See:

- [Python MSZX guide](../python/guides/mszx.md) — `MSZXBuilder`,
  `create_mszx()`
- [Node.js MSZX guide](../node/guides/mszx.md) — `MSZXBuilder`,
  `createMSZX()`
