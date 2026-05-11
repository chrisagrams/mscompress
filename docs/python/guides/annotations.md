# Annotation readers

`mscompress.annotations` reads peptide-spectrum match (PSM) files in the
formats produced by common search and rescoring tools. All readers return
a stream of `PSM` dataclass instances.

## Auto-detect by extension

```python
from mscompress.annotations import PSMReader

with PSMReader("results.pin") as reader:        # Percolator TSV
    for psm in reader:
        print(psm.scan_number, psm.peptide, psm.score)

with PSMReader("search.pep.xml") as reader:     # pepXML
    for psm in reader:
        ...
```

## Explicit readers

```python
from mscompress.annotations import TSVReader, PepXMLReader

with TSVReader("results.pin") as reader:
    ...

with PepXMLReader("search.pep.xml") as reader:
    ...
```

## Reading annotations bundled in MSZX

```python
from mscompress import MSZXFile

with MSZXFile("run.mszx") as archive:
    reader = archive.open_annotation("percolator")
    for psm in reader:
        ...
```

The MSZX reader handles zstd-compressed annotation files transparently.

## The `PSM` dataclass

| Field | Type | Notes |
|-------|------|-------|
| `scan_number` | int | Joins to `Spectrum.scan` |
| `peptide` | str | Sequence (typically with modifications stripped) |
| `charge` | int | Precursor charge |
| `score` | float | Search-engine score |
| `proteins` | list[str] | Mapped protein identifiers |
| `q_value` | float \| None | If reported (Percolator) |
| `pep` | float \| None | Posterior error probability |
| `retention_time` | float \| None | Seconds |
| `precursor_mz` | float \| None | Da |
| `modified_peptide` | str \| None | With modification notation |
| `is_decoy` | bool \| None | Decoy/target flag |
| `extra` | dict | Format-specific extra columns |

## Joining to spectra

The standard pattern is:

```python
with MSZXFile("run.mszx") as archive:
    psms_by_scan = {
        psm.scan_number: psm
        for psm in archive.open_annotation("percolator")
    }
    for s in archive.spectra:
        psm = psms_by_scan.get(s.scan)
        if psm and psm.q_value < 0.01:
            ...
```

For larger archives, build the join lazily — see the [ML dataset
adapters](ml-datasets.md) for grain-style streaming joins.
