#include <napi.h>
#include "export.h"

namespace mscompress {
    
    // Init and export functions
    Napi::Object Init(Napi::Env env, Napi::Object exports)
    {
        exports.Set("getTime", Napi::Function::New(env, GetTime));
        exports.Set("getThreads", Napi::Function::New(env, GetNumThreads));
        exports.Set("getFilesize", Napi::Function::New(env, GetFilesize));
        exports.Set("getFileDescriptor", Napi::Function::New(env, GetFileDescriptor));
        exports.Set("getOutputFileDescriptor", Napi::Function::New(env, GetOutputFileDescriptor));
        exports.Set("closeFileDescriptor", Napi::Function::New(env, CloseFileDescriptor));
        exports.Set("getFileType", Napi::Function::New(env, GetFileType));
        exports.Set("getMmapPointer", Napi::Function::New(env, CreateMmapPointer));
        exports.Set("readFromFile", Napi::Function::New(env, ReadFromFile));
        exports.Set("getAccessions", Napi::Function::New(env, GetAccessions));
        exports.Set("getPositions", Napi::Function::New(env, GetPositions));
        exports.Set("decodeBinary", Napi::Function::New(env, DecodeBinary));
        exports.Set("getZlibVersion", Napi::Function::New(env, GetZlibVersion));
        exports.Set("prepareCompression", Napi::Function::New(env, PrepareCompression));
        exports.Set("compress", Napi::Function::New(env, Compress));
        return exports;
    }

    NODE_API_MODULE(mscompress, Init);
}