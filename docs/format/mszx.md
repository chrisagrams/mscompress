# MSZX format

MSZX is a **tar archive** bundling MSZ-compressed spectra with annotation files
and a JSON manifest. It's a transport format — what you ship when you want to
give someone "the data plus the identifications."

There are two layouts, distinguished by the manifest:

| | v1 — single file | v2 — batch |
|---|---|---|
| Spectra files | exactly one (`spectra_file`) | N (`spectra_files`) |
| Manifest key | `spectra_file` | `spectra_files` + `container: "batch"` |
| Annotations | archive-level | per spectra file |
| Timestamps | `created_at` + tar `mtime` | **none** — byte-reproducible |
| `manifest.json` position | first | **last** |
| Written by | Python/Node `MSZXBuilder` | CLI `--batch`, `MSZXBatchWriter` |

Both are uncompressed POSIX USTAR tars. The `.msz` payloads inside are already
compressed by the MSZ pipeline; the container itself never is.

## v1 — single-file archive

```
archive.mszx                 (uncompressed tar)
├── manifest.json
├── spectra.msz              (or whatever the manifest names it)
└── annotations/
    ├── psm.pin              (Percolator TSV)
    ├── search.pep.xml       (pepXML)
    └── ...
```

```json
{
  "version": "1.0",
  "created_at": "2026-05-11T14:30:00Z",
  "spectra_file": "spectra.msz",
  "num_spectra": 12453,
  "annotations": [
    {
      "filename": "annotations/psm.pin",
      "format": "percolator_tsv",
      "compressed": false,
      "num_records": 8102
    }
  ],
  "join_key": "scan_number",
  "description": "DDA proteomics run #42",
  "source_file": "run42.mzML",
  "extra": { "instrument": "Orbitrap Eclipse", "study_id": "PXD012345" }
}
```

| Field | Type | Required | Purpose |
|-------|------|----------|---------|
| `version` | string | yes | Manifest version — a **string** such as `"1.0"`, not an integer |
| `created_at` | ISO-8601 timestamp | yes | Build time |
| `spectra_file` | string | yes | Path to the MSZ inside the archive |
| `num_spectra` | int | yes | Spectrum count in the MSZ |
| `annotations` | array | no | Annotation entries (see below) |
| `join_key` | string | yes | Column to join annotations against spectra |
| `description` | string | no | Free-text description |
| `source_file` | string | no | Original source filename |
| `extra` | object | no | Open dictionary for custom metadata |

## v2 — batch archive

```
cohort.mszx                  (uncompressed tar)
├── runA.msz
├── runB.msz
├── runB__2.msz              (basename collision -> __N suffix)
├── annotations/
│   └── runA.pin
└── manifest.json            (written LAST — sizes aren't known until then)
```

```json
{
  "version": "2.0",
  "container": "batch",
  "description": "cohort A",
  "extra": { "study_id": "PXD012345" },
  "spectra_files": [
    {
      "entry": "runA.msz",
      "original": "runA.mzML",
      "size": 964881,
      "num_spectra": 8213,
      "join_key": "scan_number",
      "annotations": [
        {
          "filename": "annotations/runA.pin",
          "format": "percolator_tsv",
          "compressed": true,
          "num_records": 4021
        }
      ]
    },
    { "entry": "runB.msz", "original": "runB.mzML", "size": 771204, "num_spectra": 6640 }
  ]
}
```

| Field | Type | Required | Purpose |
|-------|------|----------|---------|
| `version` | string | yes | `"2.0"` |
| `container` | string | yes | `"batch"` |
| `spectra_files` | array | yes | One record per MSZ member, in archive order |
| `description` | string | no | Archive-level free-text description |
| `extra` | object | no | Archive-level custom metadata |

### Spectra file record

| Field | Type | Required | Purpose |
|-------|------|----------|---------|
| `entry` | string | yes | Tar member name of the MSZ payload |
| `original` | string | yes | **Basename** of the source mzML |
| `size` | int | yes | Payload byte length, matching the tar header |
| `num_spectra` | int | no | Spectrum count — lets a reader list an archive without opening any member |
| `join_key` | string | no | Column to join this entry's annotations against its spectra |
| `annotations` | array | no | Annotation entries scoped to this MSZ |

Optional fields are **omitted rather than emitted as null**, so a reader written
against an earlier v2 revision still parses a newer archive.

`original` records only the basename. If you batch `runA/sample.mzML` and
`runB/sample.mzML`, the entries become `sample.msz` and `sample__2.msz` and the
source directories are not recoverable from the archive.

## Annotation entry

Used by both versions.

| Field | Type | Required | Purpose |
|-------|------|----------|---------|
| `filename` | string | yes | Path inside the archive |
| `format` | string | yes | `percolator_tsv`, `pepxml`, `tsv`, or another reader-supported format |
| `compressed` | bool | yes | Payload is zstd-compressed |
| `description` | string | no | Free-text description |
| `num_records` | int | no | Record/PSM count |

## Version negotiation

Readers parse the leading integer of `version` as the major, and treat the
presence of `spectra_files` (or `container: "batch"`) as authoritative for the
layout — a mislabeled `"1.0"` manifest carrying `spectra_files` is still read as
a batch archive rather than mis-read as single-file.

A manifest whose major exceeds what the build supports is **refused loudly**
rather than parsed on a best-effort basis. Current maximum supported major:
**2** in the C core, Python, and Node.

## Byte reproducibility

v2 archives contain no timestamps at all: the USTAR `mtime` field is hardcoded
to zero and the manifest has no `created_at` field. The same inputs and settings
therefore produce byte-identical archives, whichever producer wrote them —
the CLI, the Python binding, and the Node binding all drive the same C writer.
This is asserted by tests in all three suites.

v1 archives are **not** reproducible: they carry `created_at` and real tar
mtimes.

## Why v2 output must be seekable

A tar header records a member's size *before* its payload. v1 tars an existing
`.msz`, so the size is known up front. v2 streams compression directly into the
archive, so it writes a placeholder header, streams the payload, then seeks back
and patches the size and checksum in place.

That makes a seekable regular file mandatory — writing a v2 archive to a pipe,
FIFO, or stdout is a hard error. There is no unknown-length streaming member in
either USTAR or pax.

## Building MSZX archives

- [Python MSZX guide](../python/guides/mszx.md) — `MSZXBuilder`, `create_mszx`,
  `MSZXBatchWriter`, `compress_batch()`
- [Node.js MSZX guide](../node/guides/mszx.md) — `MSZXBuilder`, `createMSZX`,
  `MSZXBatchWriter`, `compressBatch()`
- [CLI reference](../cli/reference.md) — `--batch`
