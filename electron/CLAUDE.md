# MScompress — Electron GUI (rewrite)

This directory (`electron/`) is a **full rewrite** of the MScompress desktop app.
The legacy vanilla-JS GUI was removed. The new app is:

**Vite + Electron + React + TypeScript + Tailwind v4 + shadcn/ui**, built on the
**`mscompress` node-ts bindings** (the sibling `../node-ts` package, published as `mscompress` on npm).

## Design source of truth
The chosen design ("Split-Pane Workbench / IDE") already exists as a verified web mockup at:
`/home/cgrams/mscompress-electron-mockups/workbench/` (React+TS+Tailwind v4+shadcn, mock data).
Port that UI faithfully. Its layout: 3 panes — left rail (file Explorer + MSTransfer queue),
center tabbed workspace (Convert | QC | Queue | Archive), right Inspector — plus a top toolbar
(logo + theme toggle) and a bottom status bar (threads, mem, backend version).

## node-ts binding API (../node-ts/dist/index.d.ts)
- `read(path)` → auto-detects, returns MZMLFile | MSZFile | MSZXFile
- `MZMLFile`: `.format`, `.positions`, `.spectra` (lazy `Spectra`), `.compress(out)`, `.extract(out, opts)`, `getMzBinary/getIntenBinary/getXml(i)`
- `MSZFile`: `.decompress(out)`, `.extract(out, opts)`, random-access spectrum reads, `MSZFile.fromMszx(archive, entry)`
- `MSZXFile` / `MSZXManifest` / `MSZXBuilder` / `createMSZX`: archive + annotations (percolator_tsv/pepxml/tsv), manifest (num_spectra, join_key, source_file)
- `Spectrum`: `.index .scan .msLevel .retentionTime .size .mz .intensity .peaks .xml`
- `Spectra`: iterable, `.length`, `.get(i)` (cached)
- `RuntimeArguments`: threads, zstdCompressionLevel, mzLossy, intLossy (algos: none/cast/log/delta16/delta32/vbr)
- `DataFormat`: sourceMzFmt, sourceIntenFmt, sourceCompression, sourceTotalSpec, ...; `.toDict()` gives PSI-MS accessions
- `getNumThreads()`, `getFilesize(path)`, `getVersion()`

## Security model (Electron)
- `contextIsolation: true`, `nodeIntegration: false`, `sandbox` where possible.
- All native/binding work happens in the **main process**; renderer talks to it only via a
  typed, channel-whitelisted `contextBridge` preload. Never import `mscompress` in the renderer.

## Conventions
- TypeScript strict; no unused locals/params (build fails otherwise).
- Stay close to stock shadcn (new-york). Do not ship unstyled; don't wander far from shadcn.
- Keep KISS/DRY. Match existing style.
- Package manager: npm.
