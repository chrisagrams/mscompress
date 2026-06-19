# MSZX archives

Bundle an MSZ-compressed spectra file with search results into a single
self-describing archive.

## Build

```python
from mscompress import MSZFile, MSZXBuilder

with MSZFile("spectra.msz") as msz:
    (
        MSZXBuilder()
            .set_spectra(msz)
            .set_description("DDA run #42")
            .set_join_key("scan_number")
            .add_annotation("percolator", "results.pin", format="percolator_pin")
            .add_annotation("comet", "search.pep.xml", format="pepxml")
            .set_extra(instrument="Orbitrap Eclipse")
            .save("run42.mszx")
    )
```

## Or use the convenience function

```python
from mscompress import create_mszx

create_mszx(
    spectra="spectra.msz",
    output="run42.mszx",
    annotations=[
        ("percolator", "results.pin", "percolator_pin"),
        ("comet", "search.pep.xml", "pepxml"),
    ],
    description="DDA run #42",
    join_key="scan_number",
    extra={"instrument": "Orbitrap Eclipse"},
)
```

## Read

```python
from mscompress import MSZXFile

with MSZXFile("run42.mszx") as archive:
    print(archive.manifest.description)
    print(archive.manifest.num_spectra)

    # Iterate spectra
    for s in archive.spectra:
        ...

    # Iterate annotations
    for entry in archive.manifest.annotations:
        reader = archive.open_annotation(entry.name)
        for psm in reader:
            print(psm.scan_number, psm.peptide, psm.score)
```

## Manifest fields

See the [MSZX format spec](../../format/mszx.md) for the JSON schema.

## When to use it

- You're shipping data to a collaborator and want them to get spectra +
  identifications in one file.
- You're building an ML training dataset where each example needs to join
  a spectrum with a peptide assignment.
- You want a self-describing artifact with provenance baked in
  (`description`, `created_at`, `extra`).
