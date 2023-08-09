#include <napi.h>
#include "export.h"
#include "mscompress.h"

Napi::Number get_timeWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    double time = get_time();
    return Napi::Number::New(env, time);
}

Napi::Number get_num_threadsWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    int np = get_num_threads();
    return Napi::Number::New(env, np);
}

Napi::Number get_filesizeWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Check if at least one argument is passed
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected as argument").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    // Get the path
    Napi::String pathValue = info[0].As<Napi::String>();
    std::string path = pathValue.Utf8Value();
    char* charPath = (char*)path.c_str(); // Convert to char*

    int fs = get_filesize(charPath);
    return Napi::Number::New(env, fs);
}

Napi::Number get_fileDescriptorWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // Check if at least one argument is passed
    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String expected as argument").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0);
    }

    // Get the path
    Napi::String pathValue = info[0].As<Napi::String>();
    std::string path = pathValue.Utf8Value();
    char* charPath = (char*)path.c_str(); // Convert to char*

    int fd = open_file(charPath);
    return Napi::Number::New(env, fd);
}

Napi::Number get_fileTypeWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number argument expected").ThrowAsJavaScriptException();
        return Napi::Number::New(env, 0); // Returning a default value in case of error
    }

    int fd = info[0].As<Napi::Number>().Int32Value();
    int fileType = determine_filetype(fd);

    return Napi::Number::New(env, fileType);
}

Napi::Object mscompress::Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("getTime", Napi::Function::New(env, get_timeWrapped));
    exports.Set("getThreads", Napi::Function::New(env, get_num_threadsWrapped));
    exports.Set("getFilesize", Napi::Function::New(env, get_filesizeWrapped));
    exports.Set("getFileDescriptor", Napi::Function::New(env, get_fileDescriptorWrapped));
    exports.Set("getFileType", Napi::Function::New(env, get_fileTypeWrapped));
    return exports;
}