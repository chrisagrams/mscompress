# Concepts

Four ideas to keep in your head while you use `mscompress`:

- **[mzML](mzml.md)** — the input format, an uncompressed XML wrapper around
  base64-encoded binary arrays.
- **[MSZ](msz.md)** — the compressed format `mscompress` produces. Three
  streams (XML, m/z, intensity) compressed independently so individual spectra
  are randomly addressable.
- **[MSZX](mszx.md)** — a tar archive bundling an MSZ file with
  annotation files (pepXML, Percolator TSV) and a JSON manifest.
- **[Lossy vs lossless](lossy-vs-lossless.md)** — which compression path you
  take depends on whether you can tolerate precision loss on m/z and
  intensity arrays.
