#include "types.h"

namespace mscompress {

// Forward declarations from other files
Napi::Number GetFilesize(const Napi::CallbackInfo& info);
Napi::Number GetNumThreads(const Napi::CallbackInfo& info);
Napi::Number OpenOutputFile(const Napi::CallbackInfo& info);
Napi::Value CloseOutputFile(const Napi::CallbackInfo& info);
Napi::String GetVersion(const Napi::CallbackInfo& info);

Napi::Value GetDataFormat(const Napi::CallbackInfo& info);
Napi::Value SetCompressRuntimeVars(const Napi::CallbackInfo& info);
Napi::Value SetDecompressRuntimeVars(const Napi::CallbackInfo& info);

Napi::Value ScanMzml(const Napi::CallbackInfo& info);
Napi::Value ReadMszDivisions(const Napi::CallbackInfo& info);
Napi::Value PrepareDivisions(const Napi::CallbackInfo& info);

Napi::Value GetMzBinaryMzml(const Napi::CallbackInfo& info);
Napi::Value GetIntenBinaryMzml(const Napi::CallbackInfo& info);
Napi::Value GetXmlMzml(const Napi::CallbackInfo& info);
Napi::Value GetMzBinaryMsz(const Napi::CallbackInfo& info);
Napi::Value GetIntenBinaryMsz(const Napi::CallbackInfo& info);
Napi::Value GetXmlMsz(const Napi::CallbackInfo& info);

Napi::Value CompressMzml(const Napi::CallbackInfo& info);
Napi::Value CompressMzmlStreamOpen(const Napi::CallbackInfo& info);
Napi::Value CompressMzmlStreamRead(const Napi::CallbackInfo& info);
Napi::Value DecompressMsz(const Napi::CallbackInfo& info);

Napi::Value ExtractMzmlFiltered(const Napi::CallbackInfo& info);
Napi::Value ExtractMsz(const Napi::CallbackInfo& info);

Napi::Value ZstdCompress(const Napi::CallbackInfo& info);
Napi::Value ZstdDecompress(const Napi::CallbackInfo& info);

Napi::Value OpenMszFromArchive(const Napi::CallbackInfo& info);

Napi::Function BatchWriterInit(Napi::Env env);

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // Register FileHandle class
    FileHandle::Init(env, exports);

    // File operations
    exports.Set("getFilesize", Napi::Function::New(env, GetFilesize));
    exports.Set("getNumThreads", Napi::Function::New(env, GetNumThreads));
    exports.Set("openOutputFile", Napi::Function::New(env, OpenOutputFile));
    exports.Set("closeOutputFile", Napi::Function::New(env, CloseOutputFile));
    exports.Set("getVersion", Napi::Function::New(env, GetVersion));

    // Format detection
    exports.Set("getDataFormat", Napi::Function::New(env, GetDataFormat));
    exports.Set("setCompressRuntimeVars", Napi::Function::New(env, SetCompressRuntimeVars));
    exports.Set("setDecompressRuntimeVars", Napi::Function::New(env, SetDecompressRuntimeVars));

    // Division/position scanning
    exports.Set("scanMzml", Napi::Function::New(env, ScanMzml));
    exports.Set("readMszDivisions", Napi::Function::New(env, ReadMszDivisions));
    exports.Set("prepareDivisions", Napi::Function::New(env, PrepareDivisions));

    // Spectrum access - mzML
    exports.Set("getMzBinaryMzml", Napi::Function::New(env, GetMzBinaryMzml));
    exports.Set("getIntenBinaryMzml", Napi::Function::New(env, GetIntenBinaryMzml));
    exports.Set("getXmlMzml", Napi::Function::New(env, GetXmlMzml));

    // Spectrum access - MSZ
    exports.Set("getMzBinaryMsz", Napi::Function::New(env, GetMzBinaryMsz));
    exports.Set("getIntenBinaryMsz", Napi::Function::New(env, GetIntenBinaryMsz));
    exports.Set("getXmlMsz", Napi::Function::New(env, GetXmlMsz));

    // Compression / decompression
    exports.Set("compressMzml", Napi::Function::New(env, CompressMzml));
    exports.Set("compressMzmlStreamOpen", Napi::Function::New(env, CompressMzmlStreamOpen));
    exports.Set("compressMzmlStreamRead", Napi::Function::New(env, CompressMzmlStreamRead));
    exports.Set("decompressMsz", Napi::Function::New(env, DecompressMsz));

    // Extraction
    exports.Set("extractMzmlFiltered", Napi::Function::New(env, ExtractMzmlFiltered));
    exports.Set("extractMsz", Napi::Function::New(env, ExtractMsz));

    // Zstd utilities
    exports.Set("zstdCompress", Napi::Function::New(env, ZstdCompress));
    exports.Set("zstdDecompress", Napi::Function::New(env, ZstdDecompress));

    // MSZX archive helpers
    exports.Set("openMszFromArchive", Napi::Function::New(env, OpenMszFromArchive));

    // Batch (.mszx v2) writer class
    exports.Set("BatchWriter", BatchWriterInit(env));

    return exports;
}

NODE_API_MODULE(mscompress, Init)

} // namespace mscompress
