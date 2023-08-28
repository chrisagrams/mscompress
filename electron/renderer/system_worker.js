const mscompress = require("../../node/build/Release/mscompress.node");

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
    else if (e.data.type === "close_fd") {
        let ret = mscompress.closeFileDescriptor(e.data.fd);
        mmapStore.delete(e.data.fd);
        postMessage({
            'type': "close_fd",
            'fd': e.data.fd,
            'value': ret
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
    else if (e.data.type === "get_filetype") {
        let pointer = mmapStore.get(e.data.fd);
        postMessage({
            'type': "get_filetype",
            'fd': e.data.fd,
            'value': mscompress.getFileType(pointer, e.data.size)
        });
    }
    else if(e.data.type === "read_file") {
        let pointer = mmapStore.get(e.data.fd);
        let buffer = mscompress.readFromFile(pointer, e.data.offset, e.data.length);
        postMessage({
            'type': "read_file",
            'fd': e.data.fd,
            'offset': e.data.offset,
            'length': e.data.length,
            'value': buffer.toString('utf8')
        });
    }
    else if (e.data.type === "get_accessions"){
        let pointer = mmapStore.get(e.data.fd);
        let df = mscompress.getAccessions(pointer, e.data.size);
        postMessage({
            'type': "get_accessions",
            'fd': e.data.fd,
            'value': df
        });
    }
    else if (e.data.type === "get_positions") {
        let pointer = mmapStore.get(e.data.fd);
        let division = mscompress.getPositions(pointer, e.data.size, e.data.df);
    
        postMessage({
            'type': "get_positions",
            'fd': e.data.fd,
            'value': division
        });
    }
    else if (e.data.type === "get_zlib_version") {
        postMessage({
            'type': "get_zlib_version",
            'value': mscompress.getZlibVersion()
        });
    }
    else if (e.data.type == "decode_binary") {
        let pointer = mmapStore.get(e.data.fd);
        postMessage({
            'type': "decode_binary",
            'fd': e.data.fd,
            'value': mscompress.decodeBinary(pointer, e.data.df, e.data.start, e.data.end)
        });
    }
}

onerror = (e) => {
    console.error(e);
}