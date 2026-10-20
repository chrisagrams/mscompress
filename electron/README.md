# MScompress — Desktop GUI

A cross-platform desktop app for the [MScompress](../README.md) mass-spectrometry
compression toolkit. This is a ground-up rewrite of the previous vanilla-JS GUI.

**Stack:** Electron · Vite · React · TypeScript · Tailwind v4 · shadcn/ui
(new-york), built on the [`mscompress`](../node) Node.js/TypeScript bindings.

## Architecture

- **Main process** — all native/binding work. `src/main/bindings/` is the only
  place the native `mscompress` addon is imported; `ipc.ts` registers the
  channel-whitelisted handlers; `settings.ts` persists global settings.
- **Preload** — `src/preload/index.ts` exposes a single typed `window.api` over a
  `contextBridge`. `contextIsolation: true`, `nodeIntegration: false`.
- **Renderer** — `src/renderer/src/` (React). Talks to main **only** through
  `window.api`; it never imports `mscompress`.
- **Shared** — `src/shared/ipc.ts` holds the IPC channel list and all
  cross-process types (no electron/node imports).

Features: file Explorer + Inspector (real analysis), Convert (compress /
decompress / extract with live progress), QC dashboard
(TIC / BPC / RT×m/z heatmap / MS-levels / peaks from real spectra), batch
.mszx archiving, MSZX archive manifest + annotations viewer, and persisted settings.

## Prerequisites

Build the native binding first (the GUI loads `node`):

```sh
cd ../node && npm install && npm run build
```

## Develop

```sh
npm install        # links the local mscompress binding (file:../node)
npm run dev        # Vite dev server + Electron
```

## Build & typecheck

```sh
npm run build          # strict typecheck (main + renderer) + electron-vite build
```

## Package

electron-builder ships the native `mscompress` binding under
`resources/node_modules/mscompress` (outside the asar) so the packaged app can
load it. Output
goes to `release/`.

```sh
npm run dist:dir     # unpacked app directory (fastest; good for testing)
npm run dist:linux   # linux targets (dir + AppImage)
npm run dist         # host-platform installer
```

## Notes

- **Threads** are a global setting (Settings dialog), not a per-job control.
- Worker threads, default output dir, default preset and theme persist to
  `<userData>/settings.json`.
- Native convert calls are synchronous (the binding exposes no progress
  callback), so single-file progress is coarse.
