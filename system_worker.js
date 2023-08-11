const mscompress = require("./build/Release/mscompress.node");

const mmapStore = new Map(); // Store the mmap pointers inside system_worker so we can access them later

const mapAccession = (accession) => {
    switch(accession) {
        case 1000519:
            return "_32i_";
        case 1000520:
            return "_16e_";
        case 1000521:
            return "_32f_";
        case 1000522:
            return "_64i_";
        case 1000523:
            return "_64d_";
        
        case 1000574:
            return "_zlib_";
        case 1000576:
            return "_no_comp_";

        case 1000515:
            return "_intensity_";
        case 1000514:
            return "_mass_";
        case 1000513:
            return "_xml_";
        
        case 4700000:
            return "_lossless_";
        case 4700001:
            return "_ZSTD_compression_";
        case 4700002:
            return "_cast_64_to_32_";
        case 4700003:
            return "_log2_transform_";
        case 4700004:
            return "_delta16_transform_";
        case 4700005:
            return "_delta24_transform_";
        case 4700006:
            return "_delta32_transform_";
        case 4700007:
            return "_vbr_";
        case 4700008:
            return "_bitpack_";
        case 4700009:
            return "_vdelta16_transform_";
        case 4700010:
            return "_vdelta24_transform_";
        case 4700011:
            return "_cast_64_to_16_";
        default:
            return accession;
    }
}

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
        let mapped_df = {};
        Object.keys(df).forEach(key => {
            mapped_df[key] = mapAccession(df[key]);
        });
        postMessage({
            'type': "get_accessions",
            'fd': e.data.fd,
            'value': mapped_df // return the df with accession numbers mapped to strings
        });
    }
}

onerror = (e) => {
    console.error(e);
}