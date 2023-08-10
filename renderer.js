document.addEventListener('DOMContentLoaded', () => {
  // Handle file when dropped
  const leftContainer = document.querySelector('#left-container');
  leftContainer.addEventListener('drop', (e) => {
    e.preventDefault();

    const files = e.dataTransfer.files;

    Array.from(files).forEach(f => {
      console.log('Dropped file: ', f);
    })
  });

  // on click, have main process open system dialog
  document.querySelector("#system-dialog").addEventListener('click', e => {
    e.preventDefault();
    //Send message to main process to open the file dialog
    window.ipcRenderer.send('open-file-dialog');
  });

  // main process returns selected files from system dialog
  window.ipcRenderer.on('selected-files', (arr) => {
    console.log("Selected files: ", arr);
  })

})
const system_worker = new Worker('./system_worker.js');

console.log("Running system detection...");

system_worker.postMessage({'type': "get_threads"});
system_worker.postMessage({'type': "get_filesize", 'path': "C:\\Windows\\System32\\notepad.exe"});
system_worker.onmessage = (e) => { 
   console.log(e.data);
  //  document.querySelector('h1').innerHTML = "get_threads(): " + e.data;
  if (e.data.type == "get_fd")
  {
    console.log("get_fd(): " + e.data.value);
    system_worker.postMessage({'type': "get_filetype", 'fd': e.data.value});
  }

}
system_worker.onerror = (e) => {
  console.error(e.message, e);
};
