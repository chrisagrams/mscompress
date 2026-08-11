# `mscompress.mszx`

MSZX archive support — bundle MSZ spectra with annotation files in a
self-describing tar archive.

Single-file (v1) archives use `MSZXFile`/`MSZXBuilder`; multi-file (v2 "batch")
archives use `MSZXBatchFile`/`MSZXBatchWriter`. See the
[MSZX format spec](../../format/mszx.md).

## Single-file archives

::: mscompress.mszx.MSZXFile

::: mscompress.mszx.MSZXBuilder

::: mscompress.mszx.MSZXManifest

::: mscompress.mszx.AnnotationEntry

::: mscompress.mszx.create_mszx

## Batch archives

::: mscompress.mszx.MSZXBatchFile

::: mscompress.MSZXBatchWriter

::: mscompress.mszx.compress_batch

::: mscompress.mszx.MSZXBatchManifest

::: mscompress.mszx.SpectraFileEntry
