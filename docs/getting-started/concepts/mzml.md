# mzML and MS data

mzML is the [HUPO-PSI](https://www.psidev.info/) standard for mass spectrometry
data. It's an XML document containing a sequence of **spectra**, each of which
has:

- A **scan number** and **index** identifying it.
- An **MS level** (1 for survey, 2+ for fragmentation).
- A **retention time** in seconds.
- Two binary arrays, base64-encoded and typically zlib-compressed inside the
  XML:
  - **m/z array** — mass-to-charge ratios (`MS:1000514`)
  - **intensity array** — signal intensities (`MS:1000515`)

The binary arrays dominate the file size, and the data type accession
identifies the encoding:

| Accession | Meaning |
|-----------|---------|
| `MS:1000519` | 32-bit signed integer |
| `MS:1000520` | 16-bit float |
| `MS:1000521` | 32-bit float |
| `MS:1000522` | 64-bit integer |
| `MS:1000523` | 64-bit double |

`mscompress` parses these accessions when scanning an mzML file and uses them
to pick the right encoder for each algorithm (see
[How it works](../../algorithms/how-it-works.md)).
