import { app, dialog, ipcMain, shell, BrowserWindow } from 'electron'
import { IPC } from '../shared/ipc'
import * as bindings from './bindings'

const FILE_FILTERS = [
  { name: 'MS files', extensions: ['mzML', 'msz', 'mszx'] },
  { name: 'mzML', extensions: ['mzML'] },
  { name: 'MSZ', extensions: ['msz'] },
  { name: 'MSZX', extensions: ['mszx'] },
  { name: 'All files', extensions: ['*'] }
]

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

  ipcMain.handle(IPC.getDefaultOutputDir, () => app.getPath('downloads'))

  ipcMain.handle(IPC.getVersion, () => bindings.getVersion())

  ipcMain.handle(IPC.getNumThreads, () => bindings.getNumThreads())

  ipcMain.handle(IPC.getFilesize, (_event, path: string) => bindings.getFilesize(path))

  ipcMain.handle(IPC.analyze, (_event, path: string) => bindings.analyze(path))
}
