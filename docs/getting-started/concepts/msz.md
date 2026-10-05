# MSZ — the three-stream compressed format

MSZ rearranges an mzML file so the three things it contains (XML metadata,
m/z arrays, intensity arrays) are stored as three **independently compressed
streams**. The footer records the byte offsets of each stream, which is what
makes random-access reads possible — you can find the m/z array for spectrum
N without decompressing anything else.

```
┌──────────────────────────────────────────────────────────┐
│  Header (512 bytes)                                      │
│    magic · version · message · data_format · blocksize   │
├──────────────────────────────────────────────────────────┤
│  XML stream         (ZSTD-compressed mzML structure)     │
├──────────────────────────────────────────────────────────┤
│  m/z binary stream  (algo → optional zlib → ZSTD blocks) │
├──────────────────────────────────────────────────────────┤
│  Intensity stream   (algo → optional zlib → ZSTD blocks) │
├──────────────────────────────────────────────────────────┤
│  Block-length metadata  (per-stream block sizes)         │
├──────────────────────────────────────────────────────────┤
│  Divisions table     (spectrum → byte offset mapping)    │
├──────────────────────────────────────────────────────────┤
│  Footer (88 bytes)                                       │
│    stream offsets · divisions offset · counts · magic    │
└──────────────────────────────────────────────────────────┘
```

For the full byte-level layout, see the [MSZ format spec](../../format/msz.md).

## Why three streams?

- **Better compression.** ZSTD does much better on homogeneous data, so
  separating "XML text" from "double-precision floats" from "double-precision
  floats representing intensities" gives the compressor cleaner signals.
- **Random access.** Each stream is split into blocks. Decompressing the m/z
  array for spectrum 50,000 only requires reading two block-length entries
  and decompressing one block.
- **Independent transformation.** The m/z and intensity arrays can each get
  their own pre-compression algorithm (delta encoding, log transform, etc.)
  without touching the XML.
