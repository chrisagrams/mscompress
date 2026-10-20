#include "types.h"

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

Napi::Value DecompressMsz(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsString() || !info[2].IsObject()) {
        Napi::TypeError::New(env, "decompressMsz: (FileHandle, outputPath, ArgsObject) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    std::string outputPath = info[1].As<Napi::String>().Utf8Value();
    Arguments* args = NapiObjectToArguments(env, info[2].As<Napi::Object>());

    if (!handle->IsOpen()) {
        delete args;
        Napi::Error::New(env, "decompressMsz: file handle is closed").ThrowAsJavaScriptException();
        return env.Null();
    }

    int output_fd = open_output_file(const_cast<char*>(outputPath.c_str()));
    if (output_fd < 0) {
        delete args;
        Napi::Error::New(env, "decompressMsz: failed to open output file: " + outputPath).ThrowAsJavaScriptException();
        return env.Null();
    }

    int rv = decompress_msz(
        static_cast<char*>(handle->GetMapping()),
        handle->GetFilesize(),
        args,
        output_fd
    );

    flush(output_fd);
    close_file(output_fd);
    delete args;

    if (rv != 0) {
        Napi::Error::New(env, "decompressMsz: decompression failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    return Napi::Boolean::New(env, true);
}

} // namespace mscompress
