const mscompress = require("./build/Debug/mscompress.node");


let file = "C:\\Users\\Chris\\Downloads\\SP_HighGrade-PrCa-Set1_Fr12.mzML"
let output_file = "C:\\Users\\Chris\\Downloads\\node_test.msz"

let test_file = "C:\\Users\\Chris\\Downloads\\test.mzML"

let threads = mscompress.getThreads();
let fd = mscompress.getFileDescriptor(file);
let output_fd = mscompress.getOutputFileDescriptor(output_file);
let mmap = mscompress.getMmapPointer(fd);
let filesize = mscompress.getFilesize(file);
let df = mscompress.getAccessions(mmap, filesize);
let division = mscompress.getPositions(mmap, filesize, df);
console.log(mscompress.getZlibVersion());
let binary = mscompress.decodeBinary(mmap, df, division.mz.start_positions[0], division.mz.end_positions[0]);

let prepare = mscompress.prepareCompression(division, df, {"threads": 2});
mscompress.compress(mmap, filesize, {"threads": 2}, df, prepare,  output_fd);
mscompress.closeFileDescriptor(output_fd);

let test_output_fd = mscompress.getOutputFileDescriptor(test_file);
let input_msz = mscompress.getFileDescriptor(output_file);
let input_msz_size = mscompress.getFilesize(output_file);
let test_mmap = mscompress.getMmapPointer(input_msz);
mscompress.decompress(test_mmap, input_msz_size, {"threads": 2}, test_output_fd);

console.log(threads);