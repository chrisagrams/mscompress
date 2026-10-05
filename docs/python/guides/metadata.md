# Croissant metadata

`mscompress.metadata` generates [ML Croissant](https://mlcommons.org/croissant/)
metadata describing an MSZ or MSZX dataset, so it's directly usable from
Hugging Face Datasets and other Croissant-aware tooling.

## Single MSZ file

```python
from mscompress import build_msz_metadata

croissant = build_msz_metadata(
    msz_path="run.msz",
    name="example-dda-run",
    description="DDA proteomics run #42",
    license="CC-BY-4.0",
)

import json
with open("croissant.json", "w") as f:
    json.dump(croissant, f, indent=2)
```

## Composite: spectra + search results with a join

```python
from mscompress import build_composite_metadata

croissant = build_composite_metadata(
    msz_path="run.msz",
    annotations=[
        ("percolator", "results.pin", "percolator_pin"),
    ],
    name="example-dda-run-with-psms",
    join_on="scan_number",
)
```

## Builders

For full control, use the builder API:

```python
from mscompress import (
    MSZMetadataBuilder,
    PercolatorMetadataBuilder,
    CompositeMetadataBuilder,
    JoinDefinition,
    JoinStrategy,
)

msz = MSZMetadataBuilder("run.msz").build()
psms = PercolatorMetadataBuilder("results.pin").build()
join = JoinDefinition(
    left="spectra",
    right="psms",
    on="scan_number",
    strategy=JoinStrategy.LEFT_OUTER,
)
composite = CompositeMetadataBuilder([msz, psms], joins=[join]).build()
```

See the [reference](../reference/metadata.md) for every builder option and
the supporting type definitions (`FieldDefinition`, `RecordSetDefinition`,
`FileDistribution`, `DataCollectionInfo`).
