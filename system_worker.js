const mscompress = require("./build/Release/mscompress.node");

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
}