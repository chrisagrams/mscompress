#include "types.h"
#include <cstring>

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

Napi::Value ExtractMzmlFiltered(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // (FileHandle, outputPath, options)
    if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsString() || !info[2].IsObject()) {
        Napi::TypeError::New(env, "extractMzmlFiltered: (FileHandle, outputPath, options) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    std::string outputPath = info[1].As<Napi::String>().Utf8Value();
    Napi::Object options = info[2].As<Napi::Object>();

    if (!handle->IsOpen() || handle->GetPositions() == nullptr) {
        Napi::Error::New(env, "extractMzmlFiltered: handle not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Parse indices
    long* c_indices = nullptr;
    long indices_length = 0;
    if (options.Has("indices") && options.Get("indices").IsArray()) {
        Napi::Array indicesArr = options.Get("indices").As<Napi::Array>();
        indices_length = static_cast<long>(indicesArr.Length());
        c_indices = new long[indices_length];
        for (uint32_t i = 0; i < indicesArr.Length(); i++) {
            c_indices[i] = static_cast<long>(indicesArr.Get(i).As<Napi::Number>().Int64Value());
        }
    }

    // Parse scans
    uint32_t* c_scans = nullptr;
    long scans_length = 0;
    if (options.Has("scanNumbers") && options.Get("scanNumbers").IsArray()) {
        Napi::Array scansArr = options.Get("scanNumbers").As<Napi::Array>();
        scans_length = static_cast<long>(scansArr.Length());
        c_scans = new uint32_t[scans_length];
        for (uint32_t i = 0; i < scansArr.Length(); i++) {
            c_scans[i] = scansArr.Get(i).As<Napi::Number>().Uint32Value();
        }
    }

    // Parse ms_level
    uint16_t c_ms_level = 0;
    if (options.Has("msLevel") && options.Get("msLevel").IsNumber()) {
        c_ms_level = static_cast<uint16_t>(options.Get("msLevel").As<Napi::Number>().Uint32Value());
    }

    int output_fd = open_output_file(const_cast<char*>(outputPath.c_str()));
    if (output_fd < 0) {
        delete[] c_indices;
        delete[] c_scans;
        Napi::Error::New(env, "extractMzmlFiltered: failed to open output: " + outputPath).ThrowAsJavaScriptException();
        return env.Null();
    }

    extract_mzml_filtered(
        static_cast<char*>(handle->GetMapping()),
        handle->GetFilesize(),
        c_indices,
        indices_length,
        c_scans,
        scans_length,
        c_ms_level,
        handle->GetPositions(),
        output_fd
    );

    flush(output_fd);
    close_file(output_fd);

    delete[] c_indices;
    delete[] c_scans;

    return Napi::Boolean::New(env, true);
}

Napi::Value ExtractMsz(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    // (FileHandle, outputPath, options)
    if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsString() || !info[2].IsObject()) {
        Napi::TypeError::New(env, "extractMsz: (FileHandle, outputPath, options) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    std::string outputPath = info[1].As<Napi::String>().Utf8Value();
    Napi::Object options = info[2].As<Napi::Object>();

    if (!handle->IsOpen()) {
        Napi::Error::New(env, "extractMsz: handle not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Parse indices
    long* c_indices = nullptr;
    long indices_length = 0;
    if (options.Has("indices") && options.Get("indices").IsArray()) {
        Napi::Array indicesArr = options.Get("indices").As<Napi::Array>();
        indices_length = static_cast<long>(indicesArr.Length());
        c_indices = new long[indices_length];
        for (uint32_t i = 0; i < indicesArr.Length(); i++) {
            c_indices[i] = static_cast<long>(indicesArr.Get(i).As<Napi::Number>().Int64Value());
        }
    }

    // Parse scans
    uint32_t* c_scans = nullptr;
    long scans_length = 0;
    if (options.Has("scanNumbers") && options.Get("scanNumbers").IsArray()) {
        Napi::Array scansArr = options.Get("scanNumbers").As<Napi::Array>();
        scans_length = static_cast<long>(scansArr.Length());
        c_scans = new uint32_t[scans_length];
        for (uint32_t i = 0; i < scansArr.Length(); i++) {
            c_scans[i] = scansArr.Get(i).As<Napi::Number>().Uint32Value();
        }
    }

    // Parse ms_level
    uint16_t c_ms_level = 0;
    if (options.Has("msLevel") && options.Get("msLevel").IsNumber()) {
        c_ms_level = static_cast<uint16_t>(options.Get("msLevel").As<Napi::Number>().Uint32Value());
    }

    int output_fd = open_output_file(const_cast<char*>(outputPath.c_str()));
    if (output_fd < 0) {
        delete[] c_indices;
        delete[] c_scans;
        Napi::Error::New(env, "extractMsz: failed to open output: " + outputPath).ThrowAsJavaScriptException();
        return env.Null();
    }

    extract_msz(
        static_cast<char*>(handle->GetMapping()),
        handle->GetFilesize(),
        c_indices,
        indices_length,
        c_scans,
        scans_length,
        c_ms_level,
        output_fd
    );

    flush(output_fd);
    close_file(output_fd);

    delete[] c_indices;
    delete[] c_scans;

    return Napi::Boolean::New(env, true);
}

} // namespace mscompress
