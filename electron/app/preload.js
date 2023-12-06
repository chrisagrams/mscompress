/**
 * The preload script runs before. It has access to web APIs
 * as well as Electron's renderer process modules and some
 * polyfilled Node.js functions.
 *
 * https://www.electronjs.org/docs/latest/tutorial/sandbox
 */
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('ipcRenderer', {
    send: (channel, data) => {
        //whitelist channels
        let validChannels = ['open-browser', 'open-file-dialog', 'output-directory-dialog', 'app-metrics', 'render-tic-plot', 'get-default-path'];
        if(validChannels.includes(channel)) {
            ipcRenderer.send(channel, data);
        }
    },
    on: (channel, callback) => {
        //whitelist channels
        let validChannels = ['selected-files', 'selected-output-directory', 'current-app-metrics', 'tic-plot', 'tic-plot-status', 'tic-plot-error', 'set-default-path'];
        if(validChannels.includes(channel)) {
            ipcRenderer.on(channel, (e, ...args) => callback(...args));
        }
    }
});

window.addEventListener('DOMContentLoaded', () => {
    let leftContainer = document.querySelector("#left-container");

    leftContainer.addEventListener('dragover', e => {
        e.preventDefault();
        leftContainer.classList.add('dragover');
    });

    // Remove highlight during dragleave
    leftContainer.addEventListener('dragleave', () => {
        leftContainer.classList.remove('dragover');
    });

    // Remove highlight during file drop
    leftContainer.addEventListener('drop', e => {
        e.preventDefault();
        leftContainer.classList.remove('dragover');
    });

    // Remove loading
    document.querySelector(".loading").classList.add('hidden');

    // Add url to Gao Lab logo
    document.querySelector("#gao_lab_banner").addEventListener("click", () => {
        ipcRenderer.send("open-browser", "https://lab.gy/");
    });

    // Add url to Github repo logo
    document.querySelector("#github_logo").addEventListener("click", () => {
        ipcRenderer.send("open-browser", "https://github.com/chrisagrams/mscompress_dev");
    });
});