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

## Implemented architecture
- **Main** (`src/main/`): `index.ts` (window + app lifecycle), `bindings.ts`
  (the ONLY place `mscompress` is required — analyze / compress / decompress /
  extract / computeQC / readMszx + getVersion/getNumThreads/getFilesize),
  `ipc.ts` (all `ipcMain.handle` handlers + broadcasts), `queue.ts` (MSTransfer
  batch queue), `settings.ts` (persisted global settings).
- **Preload** (`src/preload/index.ts`): exposes the single typed `window.api`.
- **Shared** (`src/shared/ipc.ts`): channel whitelist (`IPC`) + all cross-process
  types (`Api`, `FileSummary`, `QCData`, `QueueState`, `AppSettings`, …). No
  electron/node imports — safe to reference from the renderer.
- **Renderer** (`src/renderer/src/`): `App.tsx` shell, `components/` (LeftRail,
  Inspector, ConvertTab, QCTab, ExtractQueueTab, ArchiveTab, SettingsDialog),
  `files.ts` (open-files list). Renderer reaches main only via `window.api`.
- `bindings.ts`, `queue.ts`, and `settings.ts` are kept **electron-free** so the
  Node smoke test (`npm run smoke:bindings`) can drive them directly; electron
  paths (userData, downloads, local-archive dir) are injected via
  `configureSettings()` / `configureQueue()` from `index.ts`.

## Conventions that emerged
- **Threads are global, not per-job**: they come from `settings.threads`. The
  convert IPC handlers inject it into `RuntimeArguments`; the queue gets it via
  `configureQueue({ threads })`. Do not add a per-job thread control.
- IPC option/type additions go in `src/shared/ipc.ts`; add the channel to `IPC`,
  the method to `Api`, the handler in `ipc.ts`, and the preload wiring.
- `bindings.ts` imports **types** from `../shared/ipc.ts` (note the `.ts`
  extension — needed so the standalone Node smoke test resolves it, since the
  file now also value-imports from shared).

## Packaging (electron-builder)
- Config in `electron-builder.yml`; appId `gy.lab.mscompress`, productName
  `MScompress`, icons from `assets/icons`.
- **Native addon**: `node_modules` is excluded from the asar; instead the
  `node-ts` binding (`dist` + `build/Release/mscompress.node` + `package.json`)
  is shipped via `extraResources` to `resources/node-ts`, and `bindings.ts`
  resolves it from `process.resourcesPath/node-ts/dist/index.js` when packaged
  (falling back to the `mscompress` node_modules symlink in dev / smoke test).
- Build the native binding (`node-ts`) BEFORE packaging.
  `npm run build` = typecheck + electron-vite build; `npm run dist:dir` =
  build + `electron-builder --dir` (unpacked); `npm run dist` / `dist:linux` =
  installers. Output goes to `release/` (git-ignored).
