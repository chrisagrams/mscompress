import { app, dialog, ipcMain, shell, BrowserWindow } from "electron"
import type { WebContents } from "electron"
import { basename } from "path"
import * as os from "os"
import { IPC } from "../shared/ipc"
import type {
  AppSettings,
  CompressOptions,
  ConvertProgress,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
  QCOptions,
  QueueAddRequest,
  QueueState,
  TitleBarOverlayColors,
} from "../shared/ipc"
import * as bindings from "./bindings"
import { queue } from "./queue"
import { getSettings, setSettings, onSettingsChange } from "./settings"

const FILE_FILTERS = [
  { name: "MS files", extensions: ["mzML", "msz", "mszx"] },
  { name: "mzML", extensions: ["mzML"] },
  { name: "MSZ", extensions: ["msz"] },
  { name: "MSZX", extensions: ["mszx"] },
  { name: "All files", extensions: ["*"] },
]

let progressSeq = 0

/**
 * Run a convert operation, streaming coarse start/step/done progress to the
 * renderer over the whitelisted `convert:progress` channel. The native call is
 * synchronous; we yield once after `start` so that event flushes to the
 * (separate) renderer process before the blocking work begins, keeping the UI's
 * progress indicator live.
 */
async function runConvert(
  sender: WebContents,
  op: ConvertProgress["op"],
  file: string,
  work: () => ConvertResult,
): Promise<ConvertResult> {
  const id = `cv${++progressSeq}`
  const emit = (p: Omit<ConvertProgress, "id" | "op" | "file">): void => {
    if (!sender.isDestroyed()) sender.send(IPC.convertProgress, { id, op, file, ...p })
  }
  emit({ phase: "start", message: `${op} ${file}` })
  await new Promise((resolve) => setImmediate(resolve))
  emit({ phase: "step", message: "working…" })
  const result = work()
  emit(result.error ? { phase: "error", error: result.error } : { phase: "done", result })
  return result
}

/**
 * Register every whitelisted IPC handler exactly once. All handlers use the
 * invoke/handle pattern and return plain, serializable values.
 */
export function registerIpcHandlers(): void {
  ipcMain.handle(IPC.openFiles, async (event) => {
    const win = BrowserWindow.fromWebContents(event.sender) ?? undefined
    const result = win
      ? await dialog.showOpenDialog(win, {
          properties: ["openFile", "multiSelections"],
          filters: FILE_FILTERS,
        })
      : await dialog.showOpenDialog({
          properties: ["openFile", "multiSelections"],
          filters: FILE_FILTERS,
        })
    return result.canceled ? [] : result.filePaths
  })

  ipcMain.handle(IPC.openOutputDir, async (event) => {
    const win = BrowserWindow.fromWebContents(event.sender) ?? undefined
    const opts = {
      properties: ["openDirectory", "createDirectory"] as Array<
        "openDirectory" | "createDirectory"
      >,
    }
    const result = win ? await dialog.showOpenDialog(win, opts) : await dialog.showOpenDialog(opts)
    return result.canceled || result.filePaths.length === 0 ? null : result.filePaths[0]
  })

  ipcMain.handle(IPC.openExternal, async (_event, url: string) => {
    // Only open http(s); refuse anything else to avoid launching arbitrary
    // protocol handlers from renderer input.
    if (typeof url === "string" && /^https?:\/\//i.test(url)) {
      await shell.openExternal(url)
    }
  })

  ipcMain.handle(IPC.revealInFolder, (_event, path: string) => {
    if (typeof path === "string" && path.length > 0) shell.showItemInFolder(path)
  })

  ipcMain.handle(IPC.getDefaultOutputDir, () => app.getPath("downloads"))

  ipcMain.handle(IPC.getVersion, () => bindings.getVersion())

  ipcMain.handle(IPC.getNumThreads, () => bindings.getNumThreads())

  // Live memory readout: this process's RSS vs system total/free (real values).
  ipcMain.handle(IPC.getMemory, () => ({
    rssBytes: process.memoryUsage().rss,
    totalBytes: os.totalmem(),
    freeBytes: os.freemem(),
  }))

  ipcMain.handle(IPC.getFilesize, (_event, path: string) => bindings.getFilesize(path))

  ipcMain.handle(IPC.analyze, (_event, path: string) => bindings.analyze(path))

  ipcMain.handle(IPC.computeQC, (_event, path: string, opts?: QCOptions) =>
    bindings.computeQC(path, opts),
  )

  ipcMain.handle(IPC.readMszx, (_event, path: string) => bindings.readMszx(path))

  // Threads come from global settings (not a per-job control).
  ipcMain.handle(IPC.compress, (event, path: string, opts: CompressOptions) =>
    runConvert(event.sender, "compress", basename(path), () =>
      bindings.compress(path, { ...opts, threads: getSettings().threads }),
    ),
  )

  ipcMain.handle(IPC.decompress, (event, path: string, opts: DecompressOptions) =>
    runConvert(event.sender, "decompress", basename(path), () =>
      bindings.decompress(path, { ...opts, threads: getSettings().threads }),
    ),
  )

  ipcMain.handle(IPC.extract, (event, path: string, opts: ExtractOptions) =>
    runConvert(event.sender, "extract", basename(path), () =>
      bindings.extract(path, { ...opts, threads: getSettings().threads }),
    ),
  )

  // ---- Global settings ----
  onSettingsChange((s: AppSettings) => {
    for (const win of BrowserWindow.getAllWindows()) {
      if (!win.webContents.isDestroyed()) win.webContents.send(IPC.settingsChange, s)
    }
  })
  ipcMain.handle(IPC.settingsGet, () => getSettings())
  ipcMain.handle(IPC.settingsSet, (_event, partial: Partial<AppSettings>) => setSettings(partial))

  // Windows-only: recolor the native title-bar overlay to match the app theme.
  // Guarded so it's a harmless no-op on macOS/Linux (where no overlay exists)
  // and never throws if the window wasn't created with `titleBarOverlay`.
  ipcMain.handle(IPC.setTitleBarOverlay, (event, colors: TitleBarOverlayColors) => {
    if (process.platform !== "win32") return
    const win = BrowserWindow.fromWebContents(event.sender)
    if (!win) return
    try {
      win.setTitleBarOverlay({ color: colors.color, symbolColor: colors.symbolColor })
    } catch {
      // Window wasn't created with a titleBarOverlay — nothing to recolor.
    }
  })

  // ---- MSTransfer batch queue ----
  // Broadcast every queue-state change to all renderer windows.
  queue.onUpdate((state: QueueState) => {
    for (const win of BrowserWindow.getAllWindows()) {
      if (!win.webContents.isDestroyed()) win.webContents.send(IPC.queueUpdate, state)
    }
  })

  ipcMain.handle(IPC.queueAdd, (_event, req: QueueAddRequest) => queue.add(req))
  ipcMain.handle(IPC.queueStart, () => queue.start())
  ipcMain.handle(IPC.queuePause, () => queue.pause())
  ipcMain.handle(IPC.queueClear, () => queue.clear())
  ipcMain.handle(IPC.queueRemove, (_event, id: string) => queue.remove(id))
  ipcMain.handle(IPC.queueGetState, () => queue.state())
}
