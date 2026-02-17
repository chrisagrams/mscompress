#include "types.h"

#include <sys/stat.h>

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

Napi::Number GetFilesize(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "getFilesize: string expected").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    std::string path = info[0].As<Napi::String>().Utf8Value();

    struct stat fi;
    if (stat(path.c_str(), &fi) != 0) {
        Napi::Error::New(env, "getFilesize: file not found: " + path).ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    size_t fs = get_filesize(const_cast<char*>(path.c_str()));
    return Napi::Number::New(env, static_cast<double>(fs));
}

Napi::Number GetNumThreads(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), get_num_threads());
}

Napi::Number OpenOutputFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "openOutputFile: string expected").ThrowAsJavaScriptException();
        return Napi::Number::New(env, -1);
    }

    std::string path = info[0].As<Napi::String>().Utf8Value();
    int fd = open_output_file(const_cast<char*>(path.c_str()));
    return Napi::Number::New(env, fd);
}

Napi::Value CloseOutputFile(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "closeOutputFile: number expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int fd = info[0].As<Napi::Number>().Int32Value();
    flush(fd);
    close_file(fd);
    return env.Undefined();
}

Napi::String GetVersion(const Napi::CallbackInfo& info) {
    return Napi::String::New(info.Env(), VERSION);
}

} // namespace mscompress
