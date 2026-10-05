# Format Reference

Byte-level documentation for the on-disk formats `mscompress` reads and
writes.

- **[MSZ](msz.md)** — the compressed spectra format. Header + three
  streams + block-length metadata + divisions table + footer.
- **[MSZX](mszx.md)** — the bundled archive format. A tar of an MSZ plus
  annotations plus a JSON manifest.
- **[Compatibility](compatibility.md)** — which library versions read
  which format versions.
