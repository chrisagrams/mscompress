const mscompress = require("./build/Release/mscompress.node");

const mmapStore = new Map(); // Store the mmap pointers inside system_worker so we can access them later

onmessage = (e) => {
    if (e.data.type === "get_threads") {
        postMessage({
            'type': "get_threads",
            'value': mscompress.getThreads()
        });
    }
    else if (e.data.type === "get_filesize") {
        postMessage({
            'type': "get_filesize",
            'path': e.data.path,
            'value': mscompress.getFilesize(e.data.path)
        });
    }
    else if (e.data.type === "get_fd") {
        postMessage({
            'type': "get_fd",
            'path': e.data.path,
            'value': mscompress.getFileDescriptor(e.data.path)
        });
    }
    else if (e.data.type === "get_filetype") {
        postMessage({
            'type': "get_filetype",
            'fd': e.data.fd,
            'value': mscompress.getFileType(e.data.fd)
        });
    }
    else if (e.data.type === "get_mmap") {
        let pointer = mscompress.getMmapPointer(e.data.fd);
        mmapStore.set(e.data.fd, pointer); // Store the pointer in a map so we can access it later
        postMessage({
            'type': "get_mmap",
            'fd': e.data.fd,
        });
    }
    else if(e.data.type === "get_512bytes") {
        let pointer = mmapStore.get(e.data.fd);
        let buffer = mscompress.get512BytesFromMmap(pointer, e.data.offset);
        postMessage({
            'type': "get_512bytes",
            'fd': e.data.fd,
            'offset': e.data.offset,
            'value': buffer.toString('utf8')
        });
    }
    else if (e.data.type === "get_accessions"){
        let pointer = mmapStore.get(e.data.fd);
        let df = mscompress.getAccessions(pointer);
        postMessage({
            'type': "get_accessions",
            'fd': e.data.fd,
            'value': df
        });
    }
}

onerror = (e) => {
    console.error(e);
}