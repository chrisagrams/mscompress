import { app, shell, BrowserWindow } from "electron"
import type { BrowserWindowConstructorOptions } from "electron"
import { extname, join } from "path"
import { fileURLToPath } from "url"
import { registerIpcHandlers } from "./ipc"
import { configureQueue } from "./queue"
import { configureSettings, loadSettings, onSettingsChange } from "./settings"
import { getNumThreads } from "./bindings"
import { IPC, TITLE_BAR_OVERLAY } from "../shared/ipc"

const __dirname = fileURLToPath(new URL(".", import.meta.url))

// Height of the app's top toolbar (Tailwind `h-11` = 44px). The Windows title-bar
// overlay is sized to match so the native window controls line up with that row.
// The toolbar's bottom separator is rendered as a *separate* full-width 1px line
// BELOW the header (see App.tsx), not as the header's own border — otherwise the
// Window Controls Overlay (which paints over the top-right of the header) would
// cover the border under the native buttons and break the line in that corner.
const TITLE_BAR_HEIGHT = 44

// Per-platform frameless title-bar configuration:
//  - macOS:   hidden native bar, floating (inset) traffic-light controls.
//  - Windows: hidden native bar + Window Controls Overlay — the native
//             min/max/close strip painted with theme-aware colors (recolored
//             live via the `window:setTitleBarOverlay` IPC on theme change).
//  - Linux:   default window frame (no override).
const titleBarOpts: Pick<BrowserWindowConstructorOptions, "titleBarStyle" | "titleBarOverlay"> =
  process.platform === "darwin"
    ? { titleBarStyle: "hiddenInset" }
    : process.platform === "win32"
      ? {
          titleBarStyle: "hidden",
          titleBarOverlay: { ...TITLE_BAR_OVERLAY.dark, height: TITLE_BAR_HEIGHT },
        }
      : {}

// App icon (reused from electron/assets). Packaged builds place it under resources.
const iconPath = app.isPackaged
  ? join(process.resourcesPath, "icon.png")
  : join(__dirname, "../../assets/icons/icon.png")

// Hold the window so second-instance / open-file can reach it.
let mainWindow: BrowserWindow | null = null
// Files requested before the renderer is ready (macOS open-file, cold start).
let pendingFiles: string[] = []
let rendererReady = false

/** Extensions the OS may hand us via file association / "Open with". */
const OPENABLE = new Set([".msz", ".mszx", ".mzml"])

/** Pull associated file paths out of an argv array (Windows/Linux launch). */
function filePathsFromArgv(argv: string[]): string[] {
  return argv
    .slice(1)
    .filter((a) => !a.startsWith("-"))
    .filter((a) => OPENABLE.has(extname(a).toLowerCase()))
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

function createWindow(): void {
  const win = new BrowserWindow({
    width: 1280,
    height: 800,
    minWidth: 960,
    minHeight: 640,
    show: false,
    autoHideMenuBar: true,
    title: "MScompress",
    icon: iconPath,
    backgroundColor: "#0a0a0a",
    // Frameless title bar on macOS (traffic lights) / Windows (overlay); default
    // frame on Linux. The renderer makes its top toolbar a drag region and insets
    // it past the native controls (see main.tsx / index.css / App.tsx).
    ...titleBarOpts,
    webPreferences: {
      preload: join(__dirname, "../preload/index.mjs"),
      sandbox: false,
      contextIsolation: true,
      nodeIntegration: false,
    },
  })
  mainWindow = win

  win.on("ready-to-show", () => {
    win.show()
    console.log("[main] window ready-to-show")
  })

  win.on("closed", () => {
    mainWindow = null
  })

  win.webContents.on("did-finish-load", () => {
    console.log("[main] renderer did-finish-load — window loaded OK")
    rendererReady = true
    // Flush any files requested before the renderer was mounted.
    if (pendingFiles.length) {
      win.webContents.send(IPC.openAssociatedFiles, pendingFiles)
      pendingFiles = []
    }
  })

  win.webContents.setWindowOpenHandler((details) => {
    void shell.openExternal(details.url)
    return { action: "deny" }
  })

  // Load the renderer: dev server URL in development, built HTML in production.
  const devUrl = process.env["ELECTRON_RENDERER_URL"]
  if (devUrl) {
    void win.loadURL(devUrl)
  } else {
    void win.loadFile(join(__dirname, "../renderer/index.html"))
  }
}

// Single-instance lock: a second launch (e.g. double-clicking another .msz)
// hands its argv to the primary instance via `second-instance` and quits.
const gotLock = app.requestSingleInstanceLock()
if (!gotLock) {
  app.quit()
} else {
  app.on("second-instance", (_e, argv) => {
    openInRenderer(filePathsFromArgv(argv))
    if (mainWindow) {
      if (mainWindow.isMinimized()) mainWindow.restore()
      mainWindow.focus()
    }
  })

  // macOS file association — can fire before whenReady, so register up front;
  // openInRenderer buffers until the renderer is loaded.
  app.on("open-file", (event, filePath) => {
    event.preventDefault()
    openInRenderer([filePath])
  })

  app.whenReady().then(() => {
    // Persisted global settings (userData/settings.json); defaults from the env.
    configureSettings({
      dir: app.getPath("userData"),
      defaultThreads: getNumThreads(),
      defaultOutputDir: app.getPath("downloads"),
    })
    const settings = loadSettings()

    // The `local-archive` remote destination copies finished outputs here.
    configureQueue({
      localArchiveDir: join(app.getPath("downloads"), "MScompress-Archive"),
      threads: settings.threads,
    })
    // Keep the queue's thread count in sync with settings.
    onSettingsChange((s) => configureQueue({ threads: s.threads }))

    registerIpcHandlers()
    createWindow()

    // Cold-start (Windows/Linux): the launch argv may carry an associated file.
    openInRenderer(filePathsFromArgv(process.argv))

    app.on("activate", () => {
      if (BrowserWindow.getAllWindows().length === 0) createWindow()
    })
  })
}

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit()
})
