const filehandles = [];

class FileHandle {
  static #extractFilename (path) {
    // Normalize path separators to Unix-style '/'
    const normalizedPath = path.replace(/\\/g, '/');

    // Get the last part of the path after the last '/'
    const filenameWithExtension = normalizedPath.split('/').pop();

    return filenameWithExtension;
  }

  constructor(path) {
    this.path = path;
    this.filename = FileHandle.#extractFilename(this.path);
    this.fd = -1;
    this.filesize = 0;
    this.type = -1;
    this.mmap = null;
  }

  async open() {

    const fd = await systemWorkerPromise({'type': "get_fd", 'path': this.path});

    if (fd <= 0)
      throw new Error("get_fd error");

    this.fd = fd;
    await this.get_type();
    await this.get_filesize();
    await this.get_mmap();
  }

  async close() {
    console.error("Not implemented");
    return;
  }

  async get_type() {
    if (this.fd <= 0)
      throw new Error("File not open");

    this.type = await systemWorkerPromise({'type': "get_filetype", 'fd': this.fd});

    return this.type;
  }

  async get_filesize() {
    if (this.fd <= 0) {
      throw new Error("File not open");
    }

    this.filesize = await systemWorkerPromise({'type': "get_filesize", 'path': this.path});
    
    return this.filesize;
  }

  async get_mmap() {
    if (this.fd <= 0) {
      throw new Error("File not open");
    }

    this.mmap = await systemWorkerPromise({'type': "get_mmap", 'fd': this.fd});

    return this.mmap;
  }

  async get_512bytes(offset) {
    if (this.fd <= 0) {
      throw new Error("File not open");
    }
    
    let buffer = await systemWorkerPromise({'type': "get_512bytes", 'fd': this.fd, 'offset': offset});
    
    return buffer;
  }

  async get_accessions() {
    if (this.fd <= 0) {
      throw new Error("File not open");
    }

    return await systemWorkerPromise({'type': 'get_accessions', 'fd': this.fd});
  }


  isValid() {
    return this.type == 1 || this.type == 2;
  }
}

const smartTrim = (string, maxLength) => {
  if (!string) return string;
  if (maxLength < 1) return string;
  if (string.length <= maxLength) return string;
  if (maxLength == 1) return string.substring(0,1) + '...';

  let midpoint = Math.ceil(string.length / 2);
  let toremove = string.length - maxLength;
  let lstrip = Math.ceil(toremove/2);
  let rstrip = toremove - lstrip;
  return string.substring(0, midpoint-lstrip) + '...' 
  + string.substring(midpoint+rstrip);
}

const formatBytes = (bytes, decimals = 2) => {
  if (bytes === 0) return '0 Bytes';

  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'];

  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return `${(bytes / Math.pow(k, i)).toFixed(decimals)} ${sizes[i]}`;
};

const showLoading = () => {
  document.querySelector(".loading").classList.remove('hidden');
  document.querySelector(".main").classList.add('blur');
}

const hideLoading = () => {
  document.querySelector(".loading").classList.add('hidden');
  document.querySelector(".main").classList.remove('blur');
}

const showError = (message) => {
  hideLoading(); // Hide loading in case it's showing

  document.querySelector(".error").classList.remove('hidden');
  document.querySelector(".main").classList.add('blur');
  document.querySelector(".error p").textContent = message;
}

const hideError = () => {
  document.querySelector(".error").classList.add('hidden');
  document.querySelector(".main").classList.remove('blur');
  document.querySelector(".error p").textContent = "";
}

// Add error button handler
document.querySelector(".error #error-close").addEventListener('click', () => {
  hideError();
});

document.addEventListener('DOMContentLoaded', () => {
  // Handle file when dropped
  const leftContainer = document.querySelector('#left-container');
  leftContainer.addEventListener('drop', (e) => {
    e.preventDefault();

    const files = e.dataTransfer.files;

    Array.from(files).forEach(f => {
      createFileCard(f.path);
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
    arr.forEach(f => {
      createFileCard(f);
    })
  })

});

const system_worker = new Worker('./system_worker.js');

// Create a promise for the system_worker response
const systemWorkerPromise = (message) => {
  return new Promise((resolve, reject) => {
    system_worker.onmessage = (e) => {
      // Resolve the promise when the expected type is received
      if(e.data.type === message.type) {
        resolve(e.data.value);
      }
    };

    // Post the message to the worker
    system_worker.postMessage(message);
  });
};


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

const checkPlaceholder = () => {
  if (filehandles.length == 0)
    document.querySelector(".placeholder").classList.remove('hidden');
}

const clearFileCards = () => {
  document.querySelectorAll(".fileCard").forEach(c => c.classList.remove("selected"));
}

const removeFileCard = (fd) => {
  const card = document.querySelector("#fd_" + fd);
  filehandles[filehandles.findIndex(fh => fh.fd == fd)].close();
  filehandles.splice(filehandles.findIndex(fh => fh.fd == fd), 1);
  card.remove();
  checkPlaceholder();
}

const createFileCard = async (path) => {
  showLoading();
  console.log("createFileCard: ", path);

  // Create new FileHandle
  const fh = new FileHandle(path);
  filehandles.push(fh);
  await fh.open();
  console.log(fh);

  // Check if valid
  if(fh.isValid)
  {
    // Hide placeholder
    document.querySelector(".placeholder").classList.add('hidden');

    // Create card
    const card = document.createElement('div');
    card.classList.add("fileCard");
    card.id = "fd_" + fh.fd;

    card.addEventListener("click", e => {
      clearFileCards(); // Remove selection from previous card
      card.classList.add("selected");
      // TODO: more logic on click
    })

    const type = document.createElement('h3');
    if(fh.type == 1)
    {
      type.classList.add("mzml");
      type.innerText = "mzML";
    }
    else if (fh.type == 2)
    {
      type.classList.add("msz");
      type.innerText = "msz";
    }
    else
    {
      filehandles.splice(filehandles.findIndex(h => h.fd == fh.fd), 1);
      checkPlaceholder(); // if error, make sure placeholder is shown if no cards are present
      showError("Not a valid file (mzML/msz)");
      throw new Error("Not a valid file (mzML/msz)");
    }

    const card_header = document.createElement('div');

    const close = document.createElement('button');
    close.textContent = "X";
    close.addEventListener('click', e => {
      e.stopPropagation();
      removeFileCard(fh.fd);
    });

    card_header.append(type, close);

    const name = document.createElement('h1');
    name.innerText = smartTrim(fh.filename, 25);

    const size = document.createElement('h3');
    size.innerText = formatBytes(fh.filesize);

    const p = document.createElement('p');
    p.textContent = fh.path;

    card.append(card_header, name, size, p);

    // Append card to container
    document.querySelector("#left-container").append(card);

  }
  else
  {
    filehandles.splice(filehandles.findIndex(fh => fh.fd == fd), 1);
    checkPlaceholder(); // if error, make sure placeholder is shown if no cards are present
    throw new Error("Not a valid file (mzML/msz)");
  }

  hideLoading();
}