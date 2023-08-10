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
        let validChannels = ['open-file-dialog'];
        if(validChannels.includes(channel)) {
            ipcRenderer.send(channel, data);
        }
    },
    on: (channel, callback) => {
        //whitelist channels
        let validChannels = ['selected-files'];
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

});