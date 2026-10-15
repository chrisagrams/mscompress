# MSZX format

MSZX is a **tar archive** bundling MSZ-compressed spectra with annotation files
and a JSON manifest. It's a transport format — what you ship when you want to
give someone "the data plus the identifications."

Search results (PSM annotations) are stored **only as mzTab** — see
[Search results (mzTab)](#search-results-mztab).

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
├── psm.pin.mztab.zst        (converted from Percolator output)
├── search.pep.xml.mztab.zst (converted from pepXML)
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
      "filename": "psm.pin.mztab.zst",
      "format": "mztab",
      "compressed": true,
      "num_records": 8102
    }
  ],
  "join_key": "spectra_ref",
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
├── runA.pin.mztab.zst
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
      "join_key": "spectra_ref",
      "annotations": [
        {
          "filename": "runA.pin.mztab.zst",
          "format": "mztab",
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
| `format` | string | yes | `mztab` for everything the builders and `add_psms`/`addPSMs` write; `percolator_tsv`, `pepxml` or `tsv` only in [legacy archives](#legacy-archives) |
| `compressed` | bool | yes | Payload is zstd-compressed |
| `description` | string | no | Free-text description |
| `num_records` | int | no | Record/PSM count |

Member naming: an annotation converted from `<name>` is stored as
`<name>.mztab`, plus `.zst` when compressed (`test.pepXML` →
`test.pepXML.mztab.zst`). mzTab input keeps its own name. Builders reject two
annotations with the same member name.

## Search results (mzTab)

Every annotation member is an [mzTab 1.0](https://github.com/HUPO-PSI/mzTab)
document in **Summary** mode, **Identification** type, containing only the
metadata (`MTD`) and PSM (`PSH` header + `PSM` rows) sections. Engine output —
pepXML, Percolator TSV/PIN — is converted when the archive is written and the
original file is **not** stored. Python and Node write the same layout and read
each other's files.

The text is UTF-8, tab-separated, `\n`-terminated; missing values are the
literal `null`. Readers ignore `COM` lines, blank lines, and any protein,
peptide or small-molecule sections in third-party files, and look columns up
by header name, so mzTab from other tools also reads.

### Metadata (`MTD`)

| Key | Content |
|-----|---------|
| `mzTab-version` / `mzTab-mode` / `mzTab-type` | `1.0.0` / `Summary` / `Identification` |
| `mzTab-ID` | Optional title |
| `description` | `Converted by mscompress from <format> (<file>)` |
| `ms_run[1]-location` | `file://` + the spectra file name — the MSZ for single-file archives, the source mzML for batch entries |
| `software[1]` | The search engine; `[MS, MS:1001490, Percolator, ]` for Percolator, else `[MS, MS:1001456, analysis software, <name>]`. `software[1]-setting[1]` holds `version=<version>` when known |
| `psm_search_engine_score[1]` | Primary score — a PSI-MS term where one exists (`X!Tandem:hyperscore`, `X!Tandem:expect`, `SEQUEST:xcorr`, `percolator:score`), else a user param with the engine's score name |
| `psm_search_engine_score[2]` | q-value (`percolator:Q value` or `PSM-level q-value`) |
| `psm_search_engine_score[3]` | Posterior error probability (`percolator:PEP` for Percolator) |
| `fixed_mod[k]`, `variable_mod[k]` | Searched modifications as `[CHEMMOD, CHEMMOD:+57.021465, , ]`, with `-site` (residue, `N-term`, `C-term`) and `-position` lines. When none were searched, a single `No fixed/variable modifications searched` term |
| `custom[n]` | Provenance: `[, , source_format, <format>]` and `[, , source_file, <original file name>]` |

Parameters use the mzTab `[cv, accession, name, value]` notation.

### PSM rows

There is **one row per (PSM, protein)**: a PSM mapped to N proteins becomes N
consecutive rows sharing the same `PSM_ID` and differing only in `accession`
(a PSM with no protein has a single row with `accession` `null`). Readers
group rows by `PSM_ID` back into one PSM.

| Column | Value |
|--------|-------|
| `sequence` | Peptide sequence |
| `PSM_ID` | 1-based sequential id |
| `accession`, `unique` | Protein accession; `1` if the PSM maps to exactly one protein |
| `search_engine` | Same parameter as `software[1]` |
| `search_engine_score[1..3]` | Primary score, q-value, PEP (`null` when unknown) |
| `retention_time` | Seconds |
| `charge`, `exp_mass_to_charge` | Precursor charge and m/z |
| `spectra_ref` | `ms_run[1]:index=<spectrum index>` when the index is known, else `ms_run[1]:scan=<scan number>` |
| `opt_global_cv_MS:1002217_decoy_peptide` | `1` for decoys, `0` for targets |
| `opt_global_modified_sequence`, `opt_global_rank`, `opt_global_scan_number`, `opt_global_spectrum_index`, `opt_global_precursor_mass`, `opt_global_mass_diff`, `opt_global_num_matched_ions`, `opt_global_num_total_ions` | The corresponding `PSM` fields |
| `opt_global_extra_<key>` | Engine-specific values (e.g. pepXML search scores, Percolator features), one column per distinct key |

The remaining standard columns (`database`, `database_version`,
`modifications`, `calc_mass_to_charge`, `pre`, `post`, `start`, `end`) are
`null`.

Floats are written in shortest round-trip form, which differs textually
between Python and Node (`1e-05` vs `0.00001`); compare parsed values, not
bytes. When a subset is extracted to a new archive, each mzTab is filtered by
keeping every non-PSM line verbatim and only the PSM rows of the extracted
scans.

### Legacy archives

Archives written before mzTab became the only stored format carry the
engine's own files, with `format` `percolator_tsv`, `pepxml` or `tsv` and
`join_key` `scan_number`. They remain readable — each member is parsed with
its engine-specific reader — but nothing writes them any more. Readers prefer
an mzTab entry as the primary annotation when one is present.

To convert one, run `python -m mscompress.mszx.upgrade in.mszx [-o out.mszx]`
(or `mscompress.mszx.upgrade_mszx()`): each legacy member is replaced by its
mzTab conversion and the manifest is updated; spectra are copied
byte-for-byte.

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
  `MSZXBatchWriter`, `compress_batch()`, `upgrade_mszx()`
- [Node.js MSZX guide](../node/guides/mszx.md) — `MSZXBuilder`, `createMSZX`,
  `MSZXBatchWriter`, `compressBatch()`
- [CLI reference](../cli/reference.md) — `--batch`
