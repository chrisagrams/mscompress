const mscompress = require("./build/Debug/mscompress.node");


let file = "C:\\Users\\Chris\\Downloads\\SP_HighGrade-PrCa-Set1_Fr12.mzML"

let threads = mscompress.getThreads();
let fd = mscompress.getFileDescriptor(file);
let mmap = mscompress.getMmapPointer(fd);
let filesize = mscompress.getFilesize(file);
let df = mscompress.getAccessions(mmap, filesize);
let division = mscompress.getPositions(mmap, filesize, df);
console.log(mscompress.getZlibVersion());
let binary = mscompress.decodeBinary(mmap, df, division.mz.start_positions[0], division.mz.end_positions[0]);

let divisions = mscompress.prepareCompression(division, df, {"threads": 2});

console.log(threads);