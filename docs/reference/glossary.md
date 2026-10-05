# Glossary

| Term | Definition |
|------|-----------|
| **mzML** | HUPO-PSI XML format for mass spectrometry data. Input format for `mscompress`. |
| **MSZ** | The compressed format `mscompress` produces. Three streams (XML, m/z, intensity), randomly addressable. |
| **MSZX** | A tar archive bundling an MSZ file with annotation files and a JSON manifest. |
| **Spectrum** | A single MS scan — m/z + intensity arrays plus metadata (scan number, MS level, retention time). |
| **Scan** | A single mass-spectrometry measurement. "Scan number" is the original instrument-assigned identifier. |
| **MS level** | 1 for survey scans, 2 (or higher) for fragmentation scans. |
| **m/z** | Mass-to-charge ratio, the x-axis of a spectrum. |
| **Intensity** | Signal counts at a given m/z, the y-axis of a spectrum. |
| **Peak** | An (m/z, intensity) pair within a spectrum. |
| **Retention time** | Seconds since chromatographic separation began. |
| **PSM** | Peptide-Spectrum Match — an identification result linking a peptide to a spectrum. |
| **pepXML** | XML format for peptide identifications, output by many search engines. |
| **Percolator** | A semi-supervised PSM rescoring tool. Its `.pin` files are tab-separated. |
| **Croissant** | An ML metadata format describing datasets in a structured, joinable way. |
| **Block** | A unit of ZSTD-compressed data within an MSZ stream. Block boundaries determine random-access granularity. |
| **Division** | A worker thread's chunk of an MSZ file — a contiguous range of spectra. |
| **Magic tag** | The 4-byte `0x035F51B5` identifier at the start of an MSZ file. |
| **Algorithm** | A pre-compression transform on numeric arrays, registered in `algo_registry`. |
| **Accession** | A PSI-MS ontology identifier. `mscompress` uses them to identify source data types (`MS:1000521` = 32-bit float, etc.). |
