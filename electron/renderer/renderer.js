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
    this.df = null;
    this.positions = null;
  }

  async open() {

    const fd = await systemWorkerPromise({'type': "get_fd", 'path': this.path});

    if (fd <= 0)
      throw new Error("get_fd error");

    this.fd = fd;
    await this.get_mmap();
    await this.get_filesize();
    await this.get_type();
  }

  async close() {
    const ret = await systemWorkerPromise({'type': "close_fd", 'fd': this.fd});
    if (ret != 0)
      throw new Error("close_fd error");
    return;
  }

  async get_type() {
    if (this.fd <= 0)
      throw new Error("File not open");

    this.type = await systemWorkerPromise({'type': "get_filetype", 'fd': this.fd, 'size': this.filesize});

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

  async read_file(offset, length) {
    if (this.fd <= 0) {
      throw new Error("File not open");
    }
    
    let buffer = await systemWorkerPromise({'type': "read_file", 'fd': this.fd, 'offset': offset, 'length': length});
    
    return buffer;
  }

  async get_accessions() {
    if (this.fd <= 0) {
      throw new Error("File not open");
    }

    this.df = await systemWorkerPromise({'type': 'get_accessions', 'fd': this.fd, 'size': this.filesize});
    return this.df;
  }

  async get_positions() {
    if (this.fd <= 0)
      throw new Error("File not open");

    if (this.df == null)
      await this.get_accessions();

    if (this.positions == null)
      this.positions = await systemWorkerPromise({'type': 'get_positions', 'fd': this.fd, 'df': this.df, 'size': this.filesize});

    return this.positions;
  } 

  async decode_binary(start, end) {
    if (this.fd <= 0)
    throw new Error("File not open");

    if (this.df == null)
      await this.get_accessions();

    return await systemWorkerPromise({'type': 'decode_binary', 'fd': this.fd, 'df': this.df, 'start': start, 'end': end});

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

const calculateTotalMetrics = (metrics) => {
  let totalCpuUsage = 0;
  let totalMemoryUsage = 0;

  metrics.forEach((metric) => {
    totalCpuUsage += metric.cpu.percentCPUUsage;
    totalMemoryUsage += metric.memory.workingSetSize;
  });

  return {
    totalCpuUsage,
    totalMemoryUsage
  };
};

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

  window.ipcRenderer.on('current-app-metrics', (metrics) => {
    // console.log("App metrics: ", calculateTotalMetrics(metrics));
    document.querySelector("#cpu").textContent = "CPU: " + calculateTotalMetrics(metrics).totalCpuUsage.toFixed(2) + "%";
    document.querySelector("#memory").textContent = "Mem: " + formatBytes(calculateTotalMetrics(metrics).totalMemoryUsage);
  });

  // Get app metrics every second
  setInterval(() => {
    window.ipcRenderer.send('app-metrics');
  }, 2000);

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

system_worker.onerror = (e) => {
  console.error(e.message, e);
};

const checkPlaceholder = () => {
  if (filehandles.length == 0) {
    document.querySelector(".placeholder").classList.remove('hidden');
    clearFileInfo(); // Also, clear file info
  }
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

const updateFileInfo = async (fh) => {
  const df = await systemWorkerPromise({'type': "get_accessions", 'fd': fh.fd, 'size': fh.filesize});
  console.log(df);
  
  const setElementsTextContentByClass = (className, value) => {
      const elements = document.querySelectorAll(`.${className}`);
      elements.forEach(el => el.textContent = value);
  };

  switch(df['source_mz_fmt']){
      case '_32f_':
          setElementsTextContentByClass("mz_format", "float32");
          break;
      case '_64d_':
          setElementsTextContentByClass("mz_format", "double64");
          break;
      default:
          setElementsTextContentByClass("mz_format", "...");
          break;
  }

  switch(df['source_inten_fmt']){
      case '_32f_':
          setElementsTextContentByClass("inten_format", "float32");
          break;
      case '_64d_':
          setElementsTextContentByClass("inten_format", "double64");
          break;
      default:
          setElementsTextContentByClass("inten_format", "unknown");
          break;
  }

  switch(df['source_compression']){
      case '_zlib_':
          setElementsTextContentByClass("source_format", "zlib");
          break;
      case '_no_comp_':
          setElementsTextContentByClass("source_format", "none");
          break;
      default:
          setElementsTextContentByClass("source_format", "unknown");
          break;
  }

  setElementsTextContentByClass("num_spectra", df['source_total_spec']);
  
  const fileStatsElements = document.querySelectorAll(".fileStats");
  fileStatsElements.forEach(el => el.classList.remove("blur"));
}

const clearFileInfo = () => {
  document.querySelector(".mz_format").textContent = "...";
  document.querySelector(".inten_format").textContent = "...";
  document.querySelector(".source_format").textContent = "...";
  document.querySelector(".num_spectra").textContent = "...";
  document.querySelector(".fileStats").classList.add("blur");
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
      updateFileInfo(fh);
    })

    const type = document.createElement('h3');
    if(fh.type == 1)
    {
      card.classList.add("mzml");
      type.classList.add("mzml");
      type.innerText = "mzML";
    }
    else if (fh.type == 2)
    {
      card.classList.add("msz");
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

    // Select card
    card.click();

  }
  else
  {
    filehandles.splice(filehandles.findIndex(fh => fh.fd == fd), 1);
    checkPlaceholder(); // if error, make sure placeholder is shown if no cards are present
    throw new Error("Not a valid file (mzML/msz)");
  }

  hideLoading();
}

const getSelectedFileHandle = () => {
  const selectedCard = document.querySelector(".fileCard.selected");
  if (selectedCard == null)
    return null;
  const fd = selectedCard.id.split("_")[1];
  return filehandles.find(fh => fh.fd == fd);
}

const showAnalysisWindow = (fh) => {
  const analysis_div = document.querySelector(".analysis");
  analysis_div.classList.remove("hidden");
  document.querySelector(".main").classList.add("blur");
  // Update filename
  analysis_div.querySelector("#filename").textContent = fh.filename;
  fh.get_type().then(type => { // Assign type to analysis window
    const type_text = analysis_div.querySelector("#type");
    if (type == 1) //mzML
    {
      type_text.classList.add("mzml");
      type_text.textContent = "mzML";
      
    }
    else if (type == 2) //msz
    {
      type_text.classList.add("msz");
      type_text.textContent = "msz";
    }
  });
}

const closeAnalysisWindow = () => {
  document.querySelector(".analysis").classList.add("hidden");
  document.querySelector(".main").classList.remove("blur");
}

// Add close button handler
document.querySelector(".analysis #close").addEventListener('click', () => {
  closeAnalysisWindow();
});

// Add analyze button handler
document.querySelector("#analyze").addEventListener('click', e => {
  e.preventDefault();
  const fh = getSelectedFileHandle();
  if (fh == null)
    throw new Error("No file selected");

  showLoading();
  fh.get_positions().then(positions => {
    console.log(positions);
    hideLoading();
    showAnalysisWindow(fh);
  });
})