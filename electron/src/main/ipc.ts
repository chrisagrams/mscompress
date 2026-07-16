import { app, dialog, ipcMain, shell, BrowserWindow } from 'electron'
import type { WebContents } from 'electron'
import { basename } from 'path'
import { IPC } from '../shared/ipc'
import type {
  CompressOptions,
  ConvertProgress,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
  QCOptions,
  QueueAddRequest,
  QueueState
} from '../shared/ipc'
import * as bindings from './bindings'
import { queue } from './queue'

const FILE_FILTERS = [
  { name: 'MS files', extensions: ['mzML', 'msz', 'mszx'] },
  { name: 'mzML', extensions: ['mzML'] },
  { name: 'MSZ', extensions: ['msz'] },
  { name: 'MSZX', extensions: ['mszx'] },
  { name: 'All files', extensions: ['*'] }
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
  op: ConvertProgress['op'],
  file: string,
  work: () => ConvertResult
): Promise<ConvertResult> {
  const id = `cv${++progressSeq}`
  const emit = (p: Omit<ConvertProgress, 'id' | 'op' | 'file'>): void => {
    if (!sender.isDestroyed()) sender.send(IPC.convertProgress, { id, op, file, ...p })
  }
  emit({ phase: 'start', message: `${op} ${file}` })
  await new Promise((resolve) => setImmediate(resolve))
  emit({ phase: 'step', message: 'working…' })
  const result = work()
  emit(result.error ? { phase: 'error', error: result.error } : { phase: 'done', result })
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
          properties: ['openFile', 'multiSelections'],
          filters: FILE_FILTERS
        })
      : await dialog.showOpenDialog({
          properties: ['openFile', 'multiSelections'],
          filters: FILE_FILTERS
        })
    return result.canceled ? [] : result.filePaths
  })

  ipcMain.handle(IPC.openOutputDir, async (event) => {
    const win = BrowserWindow.fromWebContents(event.sender) ?? undefined
    const opts = {
      properties: ['openDirectory', 'createDirectory'] as Array<
        'openDirectory' | 'createDirectory'
      >
    }
    const result = win
      ? await dialog.showOpenDialog(win, opts)
      : await dialog.showOpenDialog(opts)
    return result.canceled || result.filePaths.length === 0 ? null : result.filePaths[0]
  })

  ipcMain.handle(IPC.openExternal, async (_event, url: string) => {
    // Only open http(s); refuse anything else to avoid launching arbitrary
    // protocol handlers from renderer input.
    if (typeof url === 'string' && /^https?:\/\//i.test(url)) {
      await shell.openExternal(url)
    }
  })

  ipcMain.handle(IPC.revealInFolder, (_event, path: string) => {
    if (typeof path === 'string' && path.length > 0) shell.showItemInFolder(path)
  })

  ipcMain.handle(IPC.getDefaultOutputDir, () => app.getPath('downloads'))

  ipcMain.handle(IPC.getVersion, () => bindings.getVersion())

  ipcMain.handle(IPC.getNumThreads, () => bindings.getNumThreads())

  ipcMain.handle(IPC.getFilesize, (_event, path: string) => bindings.getFilesize(path))

  ipcMain.handle(IPC.analyze, (_event, path: string) => bindings.analyze(path))

  ipcMain.handle(IPC.computeQC, (_event, path: string, opts?: QCOptions) =>
    bindings.computeQC(path, opts)
  )

  ipcMain.handle(IPC.compress, (event, path: string, opts: CompressOptions) =>
    runConvert(event.sender, 'compress', basename(path), () => bindings.compress(path, opts))
  )

  ipcMain.handle(IPC.decompress, (event, path: string, opts: DecompressOptions) =>
    runConvert(event.sender, 'decompress', basename(path), () => bindings.decompress(path, opts))
  )

  ipcMain.handle(IPC.extract, (event, path: string, opts: ExtractOptions) =>
    runConvert(event.sender, 'extract', basename(path), () => bindings.extract(path, opts))
  )

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
