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

const fileInput = document.querySelector('input[type="file"]');
fileInput.addEventListener('change', (e) => {
  const file = e.target.files[0];
  if (file)
  {
    system_worker.postMessage({'type': "get_filesize", 'path': file.path});
    system_worker.postMessage({'type': "get_fd", 'path': file.path});
  }
});
