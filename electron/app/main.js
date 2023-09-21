// Modules to control application life and create native browser window
const { app, BrowserWindow, nativeImage, ipcMain, dialog, shell } = require('electron')
const path = require('path')
const { spawn } = require('child_process');

const iconPath = path.join(__dirname, 'assets/icons/icon.icns')

function createWindow () {
  // Create the browser window.
  const mainWindow = new BrowserWindow({
    width: 900,
    height: 533,
    icon: iconPath,
    autoHideMenuBar: true, // remove top menu bar
    resizable: false, // disable resizing
    // frame: false, // remove window frame
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      nodeIntegration: true,
      nodeIntegrationInWorker: true,
      contextIsolation: true,
    }
  })

  // Set the dock icon on macOS
  if (process.platform === 'darwin') {
    app.dock.setIcon(nativeImage.createFromPath(iconPath));
  }

  // and load the index.html of the app.
  mainWindow.loadFile('renderer/index.html')

  // Open the DevTools.
  // mainWindow.webContents.openDevTools()
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.whenReady().then(() => {
  createWindow()

  app.on('activate', function () {
    // On macOS it's common to re-create a window in the app when the
    // dock icon is clicked and there are no other windows open.
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

// Quit when all windows are closed, except on macOS. There, it's common
// for applications and their menu bar to stay active until the user quits
// explicitly with Cmd + Q.
app.on('window-all-closed', function () {
  if (process.platform !== 'darwin') app.quit()
})

// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and require them here.

ipcMain.on('open-browser', (e, url) => {
  shell.openExternal(url);
});

ipcMain.on('open-file-dialog', e => {
  dialog.showOpenDialog({
    properties: ['openFile', 'multiSelections'],
    filters: [
      { name: 'mzML Files', extensions: ['mzML'] },
      { name: 'msz Files', extensions: ['msz'] },
      { name: 'All Files', extensions: ['*'] }
    ]
  })
  .then(result => {
    if(!result.canceled && result.filePaths.length > 0) {
      e.sender.send('selected-files', result.filePaths);
    }
  })
  .catch(err => {
    console.error(err);
  })
});

ipcMain.on('render-tic-plot', (e, file) => {
  let completeData = "";
  const ticPlot = spawn('python3', ['../modules/python/mzml_to_tic.py', file], {
    stdio: ['pipe', 'pipe', 'pipe', 'pipe'] // stdin, stdout, stderr, and progress
  });
  ticPlot.stdout.on('data', data => {
    completeData += data.toString(); // Accumulate the base64 data
  });
  ticPlot.stderr.on('data', data => {
    console.error(data.toString());
  });
  ticPlot.on('close', code => {
    console.log(`TIC plot process exited with code ${code}`);
    e.sender.send('tic-plot', completeData);
  });
  ticPlot.stdio[3].on('data', data => {
    const message = data.toString();
    e.sender.send('tic-plot-status', message);
  })
});

// Get the current app metrics
ipcMain.on('app-metrics', e => {
  e.sender.send('current-app-metrics', app.getAppMetrics());
})