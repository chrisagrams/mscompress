import { app, shell, BrowserWindow } from "electron"
import { join } from "path"
import { fileURLToPath } from "url"
import { registerIpcHandlers } from "./ipc"
import { configureQueue } from "./queue"
import { configureSettings, loadSettings, onSettingsChange } from "./settings"
import { getNumThreads } from "./bindings"

const __dirname = fileURLToPath(new URL(".", import.meta.url))

// App icon (reused from electron/assets). Packaged builds place it under resources.
const iconPath = app.isPackaged
  ? join(process.resourcesPath, "icon.png")
  : join(__dirname, "../../assets/icons/icon.png")

function createWindow(): void {
  const mainWindow = new BrowserWindow({
    width: 1280,
    height: 800,
    minWidth: 960,
    minHeight: 640,
    show: false,
    autoHideMenuBar: true,
    title: "MScompress",
    icon: iconPath,
    backgroundColor: "#0a0a0a",
    // macOS: hide the native title bar for a cleaner look while keeping the
    // traffic-light controls floating (inset) over the content. Other platforms
    // keep their default window frame. The renderer makes its top toolbar a drag
    // region and insets it past the traffic lights (see main.tsx / index.css).
    ...(process.platform === "darwin" ? { titleBarStyle: "hiddenInset" as const } : {}),
    webPreferences: {
      preload: join(__dirname, "../preload/index.mjs"),
      sandbox: false,
      contextIsolation: true,
      nodeIntegration: false,
    },
  })

  mainWindow.on("ready-to-show", () => {
    mainWindow.show()
    console.log("[main] window ready-to-show")
  })

  mainWindow.webContents.on("did-finish-load", () => {
    console.log("[main] renderer did-finish-load — window loaded OK")
  })

  mainWindow.webContents.setWindowOpenHandler((details) => {
    void shell.openExternal(details.url)
    return { action: "deny" }
  })

  // Load the renderer: dev server URL in development, built HTML in production.
  const devUrl = process.env["ELECTRON_RENDERER_URL"]
  if (devUrl) {
    void mainWindow.loadURL(devUrl)
  } else {
    void mainWindow.loadFile(join(__dirname, "../renderer/index.html"))
  }
}

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

  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit()
})
