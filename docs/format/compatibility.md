# Compatibility

Format version is encoded in the MSZ header at bytes 4–11
(`version_major.version_minor`). The current format is **1.0**.

| Library version | MSZ format | MSZX manifest | Notes |
|-----------------|------------|---------------|-------|
| 1.0.x | 1.0 | 1 | Current stable |

## Stability promises

- **MSZ format 1.0** — frozen. Existing MSZ files will be readable by all
  future 1.x releases.
- **MSZX manifest schema** — additive only. New optional fields can be
  added, but existing fields will keep their names and meanings.
- **Algorithm IDs** — frozen. An MSZ file written with `delta32`
  (`_delta32_transform_ = 4700006`) will always be decompressible with the
  same ID. Experimental algorithms (currently `vdelta16`, `vdelta24`) are
  the exception — see [vdelta](../algorithms/catalog/vdelta.md).

## Version bump policy

A new MSZ format major version means existing files cannot be read. The
project will avoid this unless absolutely necessary; in practice it would
likely come with a one-time `mscompress migrate` tool.
