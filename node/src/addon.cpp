#include <napi.h>
#include "classes/MSZFile.h"
#include "classes/MZMLFile.h"
#include "../include/export.h"

namespace mscompress {
    // Utility functions that don't belong to a specific class
    
    Napi::String GetZlibVersion(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        const char* version = zlibVersion();
        return Napi::String::New(env, version);
    }

    Napi::Number GetNumThreads(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        int np = get_num_threads();
        return Napi::Number::New(env, np);
    }

    Napi::Number GetTime(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        double time = get_time();
        return Napi::Number::New(env, time);
    }
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    // Register classes
    mscompress::MSZFile::Init(env, exports);
    mscompress::MZMLFile::Init(env, exports);
    
    // Register utility functions
    exports.Set("getZlibVersion", Napi::Function::New(env, mscompress::GetZlibVersion));
    exports.Set("getNumThreads", Napi::Function::New(env, mscompress::GetNumThreads));
    exports.Set("getTime", Napi::Function::New(env, mscompress::GetTime));
    
    return exports;
}

NODE_API_MODULE(mscompress, Init)
