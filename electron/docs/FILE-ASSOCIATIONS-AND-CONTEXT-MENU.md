# MScompress — native `.msz` / `.mszx` OS integration (DESIGN)

> Status: **design / research only — nothing implemented.**
> Target: **Windows 11 (primary)**, with brief macOS/Linux notes.
> Scope: (A) file association so double-click / "Open with" opens the file *in*
> the MScompress Electron app; (B) an Explorer right-click **"Decompress here"**
> that runs the `mscompress` CLI to decompress the file in-place, no GUI.

---

## 0. Ground truth from the repo (what's actually there today)

**Electron app** (`electron/`, electron-vite + React + TS; electron-builder `^25.1.8`):
- `electron-builder.yml`: `appId: gy.lab.mscompress`, `productName: MScompress`.
  Win target is currently only `dir` (no installer) and there is **no
  `fileAssociations`** key yet. Native `node-ts` binding shipped via
  `extraResources` to `resources/node-ts`.
- `src/main/index.ts`: creates the window in `app.whenReady()`. **No
  single-instance lock**, **no `open-file` handler**, **no argv parsing.**
- `src/main/ipc.ts`: `IPC.openFiles` opens a native dialog and returns paths;
  `IPC.analyze` → `bindings.analyze(path)`. Filters already include
  `mzML/msz/mszx` (lines 22–28).
- `src/preload/index.ts`: exposes `window.api` (`openFiles`, `analyze`, …).
- `src/renderer/src/components/LeftRail.tsx`: the real open flow —
  `handleOpenFiles()` → `window.api.openFiles()` → `ingest(paths)` →
  `window.api.analyze(p)` → builds `FileEntry[]` → `onAddFiles(entries)`.
  `onAddFiles` is `App.addFiles` in `App.tsx` (dedupes + selects).
- Shared types in `src/shared/ipc.ts` (`IPC` channel map, `Api`, `FileKind`).

**Standalone CLI** (`cli/mscompress.c`, built by `cli/CMakeLists.txt`):
- Output binary is **`mscompress`** (`add_executable(mscompress …)`), i.e.
  **`mscompress.exe`** on Windows. C, statically links zstd/zlib/lz4/base64/yxml.
- **Usage:** `mscompress [OPTION...] input_file [output_file]`
  (`print_usage`, `cli/mscompress.c:20-93`).
- **Decompress is implicit by file type** — there is *no* `decompress`
  subcommand/flag. The tool sniffs the input:
  - `.mzML` in → **compress** → writes `input` with `.msz`
    (`change_extension(input, ".msz")`, `src/file.c:878`).
  - `.msz` in → **decompress** → writes `input` with `.msz` stripped
    (`strip_or_append_extension`, `src/file.c:696-699`): `foo.msz` → `foo.mzML`
    when the name ends in `.msz`; otherwise appends `.mzML`.
  - `.mszx` in → **archive extract** (`DECOMPRESS_MSZX`, `cli/mscompress.c:304-326,
    435-443`): default output is the input path **with `.mszx` stripped**, used
    as an **output directory** into which mzML + annotations are written.
- So the exact real invocations we care about (run from the file's own folder):
  - `mscompress "C:\path\to\file.msz"`   → produces `C:\path\to\file.mzML`
  - `mscompress "C:\path\to\file.mszx"`  → produces dir `C:\path\to\file\` with
    the mzML + annotation files inside.
- Output **always lands next to the input** when no `output_file` is given —
  exactly the "decompress in-place into its folder" behavior we want. No `--out`
  needed.
- Verify text (quote): usage line `"Usage: %s [OPTION...] input_file
  [output_file]"` and `"output_file … If not specified, the output file name is
  the input file name with extension .msz."` (`cli/mscompress.c:21, 89-92`).

> **Consequence for the context-menu design:** we invoke the CLI with a single
> quoted path argument and *no* output path. The CLI's own defaulting puts the
> result in the same folder. Nothing clever required.

---

## 1. FILE ASSOCIATION — open `.msz` / `.mszx` in the app

### 1.1 electron-builder config

electron-builder has a top-level `fileAssociations` array (applies to
mac + win; per-ext `icon`). On Windows it drives the NSIS/MSI installer to
register a ProgID + the extension and to add the app to the "Open with" list.

Add to `electron/electron-builder.yml`. Also switch the Windows target to
`nsis` (installer) so association + context-menu registry writes happen at
install time (the current `dir` target does no registration).

```yaml
# electron-builder.yml  (additions / changes)

fileAssociations:
  - ext: msz
    name: MScompress Compressed MS Data
    description: MScompress compressed mass-spectrometry file
    # role is honored on macOS (CFBundleTypeRole); ignored on Windows.
    role: Editor
    icon: assets/icons/msz.ico      # .ico for win, .icns for mac (see 1.5)
  - ext: mszx
    name: MScompress MS Archive
    description: MScompress archive (spectra + annotations)
    role: Editor
    icon: assets/icons/mszx.ico

win:
  # was: [dir] — need an installer to register associations + context menu
  target:
    - nsis
  icon: assets/icons/icon.ico       # app icon should be a real .ico for win
  # Custom NSIS include script for the context-menu registry (see section 2)
# nsis block (new):
nsis:
  oneClick: false                    # allow install-dir + per-user/all-users choice
  perMachine: false                  # default per-user (HKCU) — no elevation; see §3
  allowToChangeInstallationDirectory: true
  include: build/installer.nsh       # our custom macros (context menu, see §2)
  # electron-builder auto-generates association register/unregister; our .nsh
  # only adds the extra "Decompress here" verb.
```

Notes:
- `role` values are macOS `CFBundleTypeRole` (`Editor`/`Viewer`/`Shell`/`None`).
  Use `Editor`. Windows ignores it.
- electron-builder derives a ProgID automatically. By default it is
  `<sanitized appId-ish>.<ext>` — historically `${appInfo.id}.<ext>` style; in
  practice for appId `gy.lab.mscompress` the generated ProgID looks like
  `gy.lab.mscompress` + ext-specific class. You can pin it explicitly per assoc
  if you want deterministic keys the context-menu step can reuse — but the
  cleaner approach is to hang the context menu off
  `SystemFileAssociations\.msz` (extension-scoped, ProgID-agnostic) so the two
  features don't have to share a generated ProgID name (see §2/§3).
- Changing win target from `dir`→`nsis` also means `npm run dist` produces a
  real installer; keep a `dir` variant for quick unpacked testing if desired
  (`--dir`).

### 1.2 How the path arrives at the app

- **Windows:** the launched path is passed as a **command-line argument** in
  `process.argv`. First launch → it's in the initial `process.argv`. If the app
  is already running → Windows starts a *second* process whose argv carries the
  path; we must forward it to the running instance via the single-instance lock
  and the `second-instance` event, then focus the existing window.
- **macOS:** never argv. The path arrives via the `open-file` event (can fire
  **before** `whenReady`, so register the listener at top-level module scope and
  buffer paths until the window exists).
- **Linux:** argv, same as Windows (works with the AppImage `.desktop` MimeType,
  optional; see §4).

### 1.3 Main-process code (`src/main/index.ts`)

Full drop-in shape (integrates with the existing `createWindow`/`whenReady`):

```ts
import { app, shell, BrowserWindow } from "electron"
// … existing imports …
import { IPC } from "../shared/ipc"

// Hold the window so second-instance / open-file can reach it.
let mainWindow: BrowserWindow | null = null
// Files requested before the renderer is ready (macOS open-file, cold start).
let pendingFiles: string[] = []

const OPENABLE = new Set([".msz", ".mszx", ".mzml"])

/** Pull associated file paths out of an argv array (Windows/Linux launch). */
function filePathsFromArgv(argv: string[]): string[] {
  // argv[0] is the exe; in dev the first real arg is the app path. Filter to
  // things that look like our files and actually exist.
  return argv
    .slice(1)
    .filter((a) => !a.startsWith("-"))
    .filter((a) => OPENABLE.has(require("path").extname(a).toLowerCase()))
}

/** Send paths to the renderer's open flow, or buffer them until it's ready. */
function openInRenderer(paths: string[]): void {
  const wanted = paths.filter(Boolean)
  if (wanted.length === 0) return
  if (mainWindow && !mainWindow.webContents.isDestroyed() && rendererReady) {
    mainWindow.webContents.send(IPC.openAssociatedFiles, wanted)
    if (mainWindow.isMinimized()) mainWindow.restore()
    mainWindow.focus()
  } else {
    pendingFiles.push(...wanted)
  }
}

let rendererReady = false

// ---- Single-instance lock (Windows/Linux) --------------------------------
const gotLock = app.requestSingleInstanceLock()
if (!gotLock) {
  // A primary instance already owns the lock; hand off our argv and quit.
  app.quit()
} else {
  app.on("second-instance", (_event, argv /*, workingDir */) => {
    // Fired in the PRIMARY instance when a 2nd launch happens (e.g. user
    // double-clicked another .msz). argv is the 2nd process's command line.
    openInRenderer(filePathsFromArgv(argv))
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore()
      mainWindow.focus()
    }
  })

  // ---- macOS: open-file (can fire before whenReady) -----------------------
  app.on("open-file", (event, filePath) => {
    event.preventDefault()
    openInRenderer([filePath])
  })

  app.whenReady().then(() => {
    // … existing configureSettings / configureQueue / registerIpcHandlers …
    registerIpcHandlers()
    createWindow() // assign the created window to `mainWindow` inside here

    // Cold-start (Windows/Linux): the launch argv may carry a file.
    openInRenderer(filePathsFromArgv(process.argv))

    app.on("activate", () => {
      if (BrowserWindow.getAllWindows().length === 0) createWindow()
    })
  })
}
```

In `createWindow()`:
```ts
function createWindow(): void {
  const win = new BrowserWindow({ /* …existing opts… */ })
  mainWindow = win

  win.webContents.on("did-finish-load", () => {
    rendererReady = true
    if (pendingFiles.length) {
      win.webContents.send(IPC.openAssociatedFiles, pendingFiles)
      pendingFiles = []
    }
  })
  win.on("closed", () => { mainWindow = null })
  // … existing loadURL/loadFile …
}
```

Why a `did-finish-load` gate + buffer: on cold start the renderer isn't
mounted when argv is parsed; `webContents.send` before load is dropped. Buffer
in `pendingFiles`, flush on `did-finish-load`. macOS `open-file` frequently
fires before `whenReady` — same buffer covers it.

### 1.4 Wiring into the EXISTING renderer open flow

The renderer already turns paths into `FileEntry` via `analyze`. Reuse it —
don't duplicate. Two small, honest changes:

**(a) New one-way channel (main → renderer).** In `src/shared/ipc.ts`:
```ts
export const IPC = {
  // … existing …
  // main -> renderer: OS handed us file paths to open (association/argv).
  openAssociatedFiles: "app:openAssociatedFiles",
} as const
```
Add to the `Api` interface:
```ts
  /** Subscribe to OS "open these files" pushes (double-click / Open with). */
  onOpenAssociatedFiles(cb: (paths: string[]) => void): () => void
```

**(b) Preload** (`src/preload/index.ts`):
```ts
  onOpenAssociatedFiles: (cb) => {
    const listener = (_e: IpcRendererEvent, paths: string[]): void => cb(paths)
    ipcRenderer.on(IPC.openAssociatedFiles, listener)
    return () => ipcRenderer.removeListener(IPC.openAssociatedFiles, listener)
  },
```

**(c) Renderer.** The `ingest()` logic currently lives inside `LeftRail`. Lift a
reusable ingest into `App.tsx` (or export it) so the association push and the
dialog both funnel through the same analyze→addFiles path. In `App.tsx`:
```ts
// reuse the same analyze→FileEntry mapping the LeftRail uses
const ingestPaths = async (paths: string[]) => {
  if (!paths.length) return
  const entries = await Promise.all(
    paths.map(async (p) => {
      const s = await window.api.analyze(p)
      return { name: s.fileName, path: s.path, kind: s.kind, size: s.filesizeBytes }
    }),
  )
  addFiles(entries)          // existing dedupe + select-first
}

useEffect(() => window.api.onOpenAssociatedFiles((paths) => { void ingestPaths(paths) }), [])
```
(Best refactor: move `ingest` out of `LeftRail` into a shared `files.ts` helper
that takes `window.api` + a sink callback, and have both `LeftRail.handleOpenFiles`
and this effect call it. Keeps one code path — matches the repo's DRY note.)

Net effect: double-clicking `foo.msz` opens/raises the single app window and the
file appears in the Explorer list + gets selected, exactly like the dialog path.

### 1.5 Asset task (icons)

electron-builder needs real per-type icons:
- `assets/icons/msz.ico`, `assets/icons/mszx.ico` (Windows — multi-resolution
  .ico: 16/32/48/256).
- `assets/icons/msz.icns`, `assets/icons/mszx.icns` (macOS).
- The **app** icon for win should be a true `.ico` (currently `win.icon` points
  at `icon.png` — electron-builder can convert, but a real `.ico` is safer).
Create from the existing `electron/assets/logos/msc_logo.svg`. Track as a
separate design/asset ticket; the config above references the target filenames.

---

## 2. CONTEXT MENU — "Decompress here" (CLI, no GUI)

Goal: right-click a `.msz`/`.mszx` in Explorer → **Decompress here** → the
`mscompress` CLI runs and drops the `.mzML` (or extracted dir) next to the file.
No window, no Electron.

### 2.1 What to invoke

Ship the standalone **`mscompress.exe`** with the app and call it directly. This
is far cleaner than a headless Electron mode (see §3.2). The CLI already:
- takes a single input path,
- infers decompress from the extension,
- writes output into the same folder.

So the command line is literally:
```
"<install>\resources\cli\mscompress.exe" "%1"
```
where `%1` is the clicked file. Run it in a console window so the user sees
progress (the CLI prints phases). Optionally wrap in `cmd /c … & pause` to keep
the window open on completion/error.

Recommended command string (keeps window open so errors are visible):
```
cmd /s /c ""<install>\resources\cli\mscompress.exe" "%1" & echo. & pause"
```

### 2.2 Registry approach — use `SystemFileAssociations` (extension-scoped)

Two placement options:

- **`HKCR\<ProgID>\shell\decompress\command`** — attaches the verb to the app's
  ProgID. Downside: couples to electron-builder's generated ProgID name and the
  verb disappears if the user changes the default app for the type.
- **`HKCR\SystemFileAssociations\.msz\shell\decompress\command`** — attaches the
  verb to the **extension itself**, independent of the default handler/ProgID.
  This is the right choice: the "Decompress here" action should always be
  present on `.msz`/`.mszx` regardless of what app currently owns "open".

`HKCR` is a merged view of `HKLM\Software\Classes` (per-machine) and
`HKCU\Software\Classes` (per-user). Write to `HKCU\Software\Classes\…` for a
per-user install (no elevation), `HKLM\Software\Classes\…` for all-users.

Exact keys/values (per-user shown; swap `HKCU`→`HKLM` for all-users):

```
HKCU\Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress
    (Default)            = "Decompress here"
    Icon                 = "<install>\resources\cli\mscompress.exe,0"

HKCU\Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress\command
    (Default)            = cmd /s /c ""<install>\resources\cli\mscompress.exe" "%1" & echo. & pause"

HKCU\Software\Classes\SystemFileAssociations\.mszx\shell\MScompressDecompress
    (Default)            = "Decompress here"
    Icon                 = "<install>\resources\cli\mscompress.exe,0"

HKCU\Software\Classes\SystemFileAssociations\.mszx\shell\MScompressDecompress\command
    (Default)            = cmd /s /c ""<install>\resources\cli\mscompress.exe" "%1" & echo. & pause"
```

Notes:
- Verb key name `MScompressDecompress` is our stable internal id; the
  `(Default)` string is the visible label.
- `%1` MUST be quoted in the command (paths have spaces).
- The `& pause` keeps the console open; drop it for a fire-and-forget flash.
- Add `Position` or `Extended` values if you want to hide it behind Shift or
  order it — optional.

### 2.3 Installing the registry entries via NSIS

electron-builder (NSIS target) supports a custom include script referenced by
`nsis.include` (`build/installer.nsh`). It exposes the standard NSIS macros
`customInstall` / `customUnInstall`. Ship the CLI first (see §2.5), then:

`electron/build/installer.nsh`:
```nsis
; Custom NSIS include merged into electron-builder's generated installer.
; Adds "Decompress here" context-menu verbs for .msz/.mszx and removes them
; on uninstall. Writes under the same root electron-builder used for the app
; (HKCU for per-user / HKLM for per-machine) via SHCTX.

!macro customInstall
  ; $INSTDIR is the app install dir; the CLI is shipped to resources\cli.
  ; SHCTX = HKCU when per-user install, HKLM when per-machine (set by e-b).
  DetailPrint "Registering MScompress 'Decompress here' context menu…"

  ; ---- .msz ----
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress" "" "Decompress here"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress" "Icon" "$INSTDIR\resources\cli\mscompress.exe,0"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress\command" "" 'cmd /s /c ""$INSTDIR\resources\cli\mscompress.exe" "%1" & echo. & pause"'

  ; ---- .mszx ----
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressDecompress" "" "Decompress here"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressDecompress" "Icon" "$INSTDIR\resources\cli\mscompress.exe,0"
  WriteRegStr SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressDecompress\command" "" 'cmd /s /c ""$INSTDIR\resources\cli\mscompress.exe" "%1" & echo. & pause"'

  ; Nudge Explorer to reload associations/icons.
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend

!macro customUnInstall
  DetailPrint "Removing MScompress context-menu entries…"
  DeleteRegKey SHCTX "Software\Classes\SystemFileAssociations\.msz\shell\MScompressDecompress"
  DeleteRegKey SHCTX "Software\Classes\SystemFileAssociations\.mszx\shell\MScompressDecompress"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
!macroend
```

Key points:
- `SHCTX` is set by electron-builder's NSIS to `HKCU` or `HKLM` matching the
  install scope, so the same script works for both — no elevation assumptions
  baked in.
- Note NSIS string quoting: the whole value is wrapped in single quotes `'…'`
  so the embedded double-quotes survive. `%1` is literal (NSIS doesn't expand
  it; Explorer does at click time).
- `SHChangeNotify(SHCNE_ASSOCCHANGED=0x08000000)` refreshes Explorer.

### 2.4 Shipping the CLI binary vs. headless Electron

**Recommendation: ship the prebuilt `mscompress.exe` and shell out to it.**

- The CLI is a ~single self-contained native exe (statically links its vendored
  zstd/zlib/lz4/base64/yxml — see `cli/CMakeLists.txt`), starts in milliseconds,
  needs no Node/Electron runtime, and already does exactly the in-place
  decompress we want.
- A "headless Electron mode" (`MScompress.exe --decompress %1 --no-gui`) means
  spinning up the whole Chromium/Node runtime just to call
  `bindings.decompress` — hundreds of MB resident, slow cold start, and you'd
  still have to suppress window creation and route argv. Strictly worse for a
  no-GUI right-click action.

### 2.5 Getting the CLI onto the machine

Bundle it via electron-builder `extraResources`, mirroring how `node-ts` is
already shipped. Build `cli/` for win-x64 in CI, drop the exe where the config
expects it, then:

```yaml
# electron-builder.yml — add alongside the existing node-ts extraResources
extraResources:
  - from: ../node-ts/dist
    to: node-ts/dist
  - from: ../node-ts/build/Release/mscompress.node
    to: node-ts/build/Release/mscompress.node
  - from: ../node-ts/package.json
    to: node-ts/package.json
  # NEW: standalone CLI for the context-menu action
  - from: ../cli/build/Release/mscompress.exe   # adjust to real CI output path
    to: cli/mscompress.exe
```
This lands the exe at `<install>\resources\cli\mscompress.exe`, which is exactly
the path the NSIS macro registers. **Do not** rely on PATH — reference the
absolute install path in the registry command so it works regardless of the
user's environment. (If a global CLI install already exists, that's a separate
distribution; for the app's context menu, self-contained is the clean path.)

macOS/Linux CLI output name is `mscompress` (no `.exe`); the `from:` path is
platform-specific, so gate the CLI extraResource per-platform if you package
those (electron-builder allows platform-scoped `extraResources` under
`mac:`/`linux:` blocks).

---

## 3. Tradeoffs, gotchas, recommended path

### 3.1 Per-user (HKCU) vs per-machine (HKLM) / elevation
- **Per-user (HKCU / `perMachine: false`)** — no UAC prompt, installs to
  `%LOCALAPPDATA%`, registry under `HKCU\Software\Classes`. Best default for a
  research tool the user installs for themselves. `SHCTX` resolves to HKCU.
- **Per-machine (HKLM / `perMachine: true`)** — requires elevation, visible to
  all users, registry under `HKLM\Software\Classes`. Only if IT deploys it.
- Because the NSIS macro uses `SHCTX`, both work with the same script. Match
  `nsis.perMachine` to your distribution model. **Recommend per-user.**

### 3.2 Windows 11 context menu (the big gotcha)
- Win11 introduced a new **compact** context menu backed by `IExplorerCommand`
  COM handlers packaged in an MSIX/sparse package. **Classic
  `SystemFileAssociations\…\shell\…` registry verbs still work**, but on Win11
  they appear under **"Show more options"** (Shift+F10 shows them directly), not
  the top-level compact menu.
- Getting into the *top-level* Win11 menu requires an `IExplorerCommand` COM DLL
  registered via a sparse/MSIX package — substantially more work (a native COM
  component, packaging identity, signing). **Recommend: ship the classic
  registry verb now** (works on Win10 fully, Win11 under "Show more options"),
  and treat the `IExplorerCommand` top-level entry as a later enhancement if
  users complain about the extra click.

### 3.3 Keeping association + context menu in sync
- Association is managed by electron-builder from `fileAssociations` (register on
  install, unregister on uninstall). The context menu is our `installer.nsh`
  add-on. Both are wired into the **same** NSIS install/uninstall lifecycle, so
  they're created and torn down together. Keep the extension list identical in
  both places (`.msz`, `.mszx`).
- Use extension-scoped `SystemFileAssociations` for the verb (not the generated
  ProgID) so a change of default "open" app doesn't strip the "Decompress here"
  action.

### 3.4 Uninstall cleanup
- `customUnInstall` deletes both verb keys. electron-builder removes its own
  association/ProgID keys. Verify nothing is left under
  `HKCU\Software\Classes\SystemFileAssociations\.msz\shell` after uninstall.
- Gotcha: if the user ran a per-user *and* a per-machine install at different
  times, keys can exist in both hives; the uninstaller only cleans the hive it
  installed to (`SHCTX`). Acceptable; note it.

### 3.5 Other gotchas
- **Path quoting**: every `%1` and every embedded exe path must be quoted;
  MS data files often live under paths with spaces.
- **Long-running decompress**: large `.msz` can take a while; the `& pause`
  console keeps the user informed. Without a console the click looks like it did
  nothing until the file appears.
- **`.mszx` output is a directory**, not a single file — make the verb label or
  docs clear ("Extract archive here") so users aren't surprised. Consider a
  distinct label per extension: "Decompress here" for `.msz`, "Extract here" for
  `.mszx`.
- **Overwrite**: the CLI writes `foo.mzML`; if it exists it's overwritten
  (`open_output_file` truncates). Note for users; no prompt in CLI mode.
- **Antivirus / SmartScreen**: an unsigned exe launched from a context menu may
  trip SmartScreen. Code-sign both `MScompress.exe` and `mscompress.exe` for a
  clean UX (separate signing ticket).
- **Single-instance lock interaction**: the lock is global to the app; the
  context-menu path never launches Electron, so no interaction there. The lock
  only matters for the association (open-in-app) path — covered in §1.3.

### 3.6 macOS / Linux equivalents (brief)
- **macOS association**: `fileAssociations` + `role` populates
  `CFBundleDocumentTypes` in Info.plist; the `open-file` event delivers paths.
  No context-menu registry concept; the nearest analog is a **Quick Action /
  Automator Service** or a Finder extension — out of scope, note as future work.
- **Linux association**: ship a `.desktop` file with `MimeType=application/…`
  and register a MIME type via `shared-mime-info` (XML + `update-mime-database`).
  argv delivers the path (same code path as Windows). A "Decompress here" entry
  is file-manager-specific (Nautilus scripts / KDE ServiceMenus) — future work.

---

## 4. Implementation checklist (ordered — hand to a coding agent)

**Phase A — File association (open-in-app)**
1. Create per-type icons: `assets/icons/msz.ico`, `mszx.ico` (+ `.icns` for mac),
   and a real `assets/icons/icon.ico` app icon, derived from `msc_logo.svg`.
2. `electron-builder.yml`: add the `fileAssociations` block (§1.1); change
   `win.target` `dir`→`nsis`; add the `nsis` block (`oneClick:false`,
   `perMachine:false`, `include: build/installer.nsh`); set `win.icon` to the
   real `.ico`.
3. `src/shared/ipc.ts`: add `IPC.openAssociatedFiles` channel + the
   `onOpenAssociatedFiles` method to the `Api` interface.
4. `src/preload/index.ts`: implement `onOpenAssociatedFiles` (ipcRenderer.on +
   unsubscribe).
5. `src/main/index.ts`: add `requestSingleInstanceLock()`; on failure
   `app.quit()`. In the primary: `second-instance` handler (parse argv → forward
   → focus), macOS `open-file` handler (top-level, `preventDefault`), argv parse
   on cold start, `pendingFiles` buffer flushed on `did-finish-load`, keep a
   module-level `mainWindow` ref set in `createWindow`.
6. Renderer: extract the `analyze→FileEntry` ingest into a shared helper
   (`files.ts`); call it from both `LeftRail.handleOpenFiles` and a new
   `App.tsx` effect subscribed to `onOpenAssociatedFiles`.
7. Build + package (`npm run dist` on Windows) → install → verify:
   double-click `.msz` opens app + loads file; double-click a 2nd file while
   running focuses the same window and adds it (no 2nd instance).

**Phase B — Context menu (CLI decompress)**
8. Build the standalone CLI for win-x64 (`cli/` via CMake) in CI; produce
   `mscompress.exe`.
9. `electron-builder.yml`: add the CLI to `extraResources`
   (`../cli/.../mscompress.exe` → `cli/mscompress.exe`), platform-scoped as
   needed.
10. Create `electron/build/installer.nsh` with `customInstall` /
    `customUnInstall` writing the `SystemFileAssociations\.msz` and `.mszx`
    `shell\MScompressDecompress\command` verbs to `SHCTX`, pointing at
    `$INSTDIR\resources\cli\mscompress.exe "%1"`, plus `SHChangeNotify`.
11. Install → right-click a `.msz` → (Win11: "Show more options" →)
    "Decompress here" → confirm `foo.mzML` appears next to it; right-click a
    `.mszx` → confirm the extracted directory appears.
12. Uninstall → confirm both verb keys and the association keys are gone.

**Phase C — polish (optional / later)**
13. Code-sign `MScompress.exe` and `mscompress.exe` (SmartScreen).
14. Distinct verb labels ("Decompress here" vs "Extract here").
15. Win11 top-level `IExplorerCommand` handler (sparse/MSIX) if the "Show more
    options" click proves annoying.
16. macOS Quick Action + Linux `.desktop`/MIME + file-manager service menus.

---

### Appendix — exact CLI facts (for the coding agent, quoted from repo)
- Binary target: `add_executable(mscompress ${SOURCES})` — `cli/CMakeLists.txt:50`.
- Usage: `"Usage: %s [OPTION...] input_file [output_file]"` — `cli/mscompress.c:21`.
- Decompress is by file type, no subcommand (`case DECOMPRESS: decompress_msz(...)`)
  — `cli/mscompress.c:384-394`.
- `.msz` → strip ext → `.mzML`: `strip_or_append_extension`, `src/file.c:683-702`;
  chosen at `src/file.c:881-882`.
- `.mszx` → strip `.mszx` → output **directory**, `DECOMPRESS_MSZX` path —
  `cli/mscompress.c:307-326, 435-443`.
- Output defaults next to input when no `output_file` given —
  `cli/mscompress.c:89-92`, `src/file.c:877-884`.
