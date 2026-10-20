#include "types.h"

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

Napi::Value GetDataFormat(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "getDataFormat: FileHandle expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    if (!handle->IsOpen()) {
        Napi::Error::New(env, "getDataFormat: file handle is closed").ThrowAsJavaScriptException();
        return env.Null();
    }

    data_format_t* df = nullptr;

    if (handle->GetFiletype() == COMPRESS) {
        // mzML
        df = pattern_detect(static_cast<char*>(handle->GetMapping()));
    } else if (handle->GetFiletype() == DECOMPRESS) {
        // MSZ
        df = get_header_df(handle->GetMapping());
    } else if (handle->GetFiletype() == EXTERNAL) {
        df = create_external_df();
    } else {
        Napi::Error::New(env, "getDataFormat: unsupported file type").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (df == nullptr) {
        Napi::Error::New(env, "getDataFormat: failed to detect data format").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Store on handle for later use
    handle->SetDataFormat(df);

    return CreateDataFormatObject(env, df);
}

Napi::Value SetCompressRuntimeVars(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsObject()) {
        Napi::TypeError::New(env, "setCompressRuntimeVars: (FileHandle, ArgsObject) expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    Arguments* args = NapiObjectToArguments(env, info[1].As<Napi::Object>());

    if (handle->GetDataFormat() == nullptr) {
        delete args;
        Napi::Error::New(env, "setCompressRuntimeVars: data format not initialized").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    set_compress_runtime_variables(args, handle->GetDataFormat());

    delete args;
    return env.Undefined();
}

Napi::Value SetDecompressRuntimeVars(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsObject()) {
        Napi::TypeError::New(env, "setDecompressRuntimeVars: FileHandle expected").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());

    if (handle->GetDataFormat() == nullptr || handle->GetFooter() == nullptr) {
        Napi::Error::New(env, "setDecompressRuntimeVars: format/footer not initialized").ThrowAsJavaScriptException();
        return env.Undefined();
    }

    set_decompress_runtime_variables(handle->GetDataFormat(), handle->GetFooter());
    return env.Undefined();
}

} // namespace mscompress
