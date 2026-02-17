#include "types.h"
#include <cstring>

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

// Get m/z binary for mzML file (decode from base64+zlib)
Napi::Value GetMzBinaryMzml(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsNumber() || !info[2].IsNumber()) {
        Napi::TypeError::New(env, "getMzBinaryMzml: (FileHandle, index, blocksize) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    size_t index = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());
    long blocksize = static_cast<long>(info[2].As<Napi::Number>().Int64Value());

    if (!handle->IsOpen() || handle->GetPositions() == nullptr || handle->GetDataFormat() == nullptr) {
        Napi::Error::New(env, "getMzBinaryMzml: handle not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    division_t* positions = handle->GetPositions();
    data_format_t* df = handle->GetDataFormat();

    uint64_t start = positions->mz->start_positions[index];
    uint64_t end = positions->mz->end_positions[index];

    char* mapping_ptr = static_cast<char*>(handle->GetMapping()) + start;
    size_t src_len = end - start;

    char* dest = nullptr;
    size_t out_len = 0;
    data_block_t* tmp = alloc_data_block(static_cast<size_t>(blocksize));

    df->decode_source_compression_mz_fun(handle->GetZStream(), mapping_ptr, src_len, &dest, &out_len, tmp);

    if (dest == nullptr) {
        dealloc_data_block(tmp);
        Napi::Error::New(env, "getMzBinaryMzml: decode failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Skip ZLIB_SIZE_OFFSET header
    char* data = dest + ZLIB_SIZE_OFFSET;
    size_t data_len = out_len - ZLIB_SIZE_OFFSET;

    Napi::Value result;

    if (df->source_mz_fmt == _64d_) {
        size_t count = data_len / sizeof(double);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(double));
        std::memcpy(buf.Data(), data, count * sizeof(double));
        result = Napi::Float64Array::New(env, count, buf, 0);
    } else if (df->source_mz_fmt == _32f_) {
        size_t count = data_len / sizeof(float);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(float));
        std::memcpy(buf.Data(), data, count * sizeof(float));
        result = Napi::Float32Array::New(env, count, buf, 0);
    } else {
        free(dest);
        dealloc_data_block(tmp);
        Napi::Error::New(env, "getMzBinaryMzml: unsupported data format").ThrowAsJavaScriptException();
        return env.Null();
    }

    free(dest);
    dealloc_data_block(tmp);
    return result;
}

// Get intensity binary for mzML file
Napi::Value GetIntenBinaryMzml(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 3 || !info[0].IsObject() || !info[1].IsNumber() || !info[2].IsNumber()) {
        Napi::TypeError::New(env, "getIntenBinaryMzml: (FileHandle, index, blocksize) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    size_t index = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());
    long blocksize = static_cast<long>(info[2].As<Napi::Number>().Int64Value());

    division_t* positions = handle->GetPositions();
    data_format_t* df = handle->GetDataFormat();

    uint64_t start = positions->inten->start_positions[index];
    uint64_t end = positions->inten->end_positions[index];

    char* mapping_ptr = static_cast<char*>(handle->GetMapping()) + start;
    size_t src_len = end - start;

    char* dest = nullptr;
    size_t out_len = 0;
    data_block_t* tmp = alloc_data_block(static_cast<size_t>(blocksize));

    df->decode_source_compression_inten_fun(handle->GetZStream(), mapping_ptr, src_len, &dest, &out_len, tmp);

    if (dest == nullptr) {
        dealloc_data_block(tmp);
        Napi::Error::New(env, "getIntenBinaryMzml: decode failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    char* data = dest + ZLIB_SIZE_OFFSET;
    size_t data_len = out_len - ZLIB_SIZE_OFFSET;

    Napi::Value result;

    if (df->source_inten_fmt == _64d_) {
        size_t count = data_len / sizeof(double);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(double));
        std::memcpy(buf.Data(), data, count * sizeof(double));
        result = Napi::Float64Array::New(env, count, buf, 0);
    } else if (df->source_inten_fmt == _32f_) {
        size_t count = data_len / sizeof(float);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(float));
        std::memcpy(buf.Data(), data, count * sizeof(float));
        result = Napi::Float32Array::New(env, count, buf, 0);
    } else {
        free(dest);
        dealloc_data_block(tmp);
        Napi::Error::New(env, "getIntenBinaryMzml: unsupported data format").ThrowAsJavaScriptException();
        return env.Null();
    }

    free(dest);
    dealloc_data_block(tmp);
    return result;
}

// Get XML for mzML file
Napi::Value GetXmlMzml(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "getXmlMzml: (FileHandle, index) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    size_t index = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());

    division_t* positions = handle->GetPositions();
    uint64_t start = positions->spectra->start_positions[index];
    uint64_t end = positions->spectra->end_positions[index];
    size_t size = end - start;

    char* mapping_ptr = static_cast<char*>(handle->GetMapping()) + start;

    // Copy to null-terminated string
    char* res = static_cast<char*>(malloc(size + 1));
    std::memcpy(res, mapping_ptr, size);
    res[size] = '\0';

    Napi::String result = Napi::String::New(env, res, size);
    free(res);

    return result;
}

// Get m/z binary for MSZ file (decompress from compressed blocks)
Napi::Value GetMzBinaryMsz(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "getMzBinaryMsz: (FileHandle, index) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    size_t index = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());

    size_t out_len = 0;
    char* res = extract_spectrum_mz(
        static_cast<char*>(handle->GetMapping()),
        handle->GetDctx(),
        handle->GetDataFormat(),
        handle->GetMzBlockLens(),
        static_cast<long>(handle->GetFooter()->mz_binary_pos),
        handle->GetDivisions(),
        static_cast<long>(index),
        &out_len,
        FALSE
    );

    if (res == nullptr) {
        Napi::Error::New(env, "getMzBinaryMsz: extraction failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    data_format_t* df = handle->GetDataFormat();
    Napi::Value result;

    if (df->source_mz_fmt == _64d_) {
        size_t count = out_len / sizeof(double);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(double));
        std::memcpy(buf.Data(), res, count * sizeof(double));
        result = Napi::Float64Array::New(env, count, buf, 0);
    } else if (df->source_mz_fmt == _32f_) {
        size_t count = out_len / sizeof(float);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(float));
        std::memcpy(buf.Data(), res, count * sizeof(float));
        result = Napi::Float32Array::New(env, count, buf, 0);
    } else {
        free(res);
        Napi::Error::New(env, "getMzBinaryMsz: unsupported format").ThrowAsJavaScriptException();
        return env.Null();
    }

    free(res);
    return result;
}

// Get intensity binary for MSZ file
Napi::Value GetIntenBinaryMsz(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "getIntenBinaryMsz: (FileHandle, index) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    size_t index = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());

    size_t out_len = 0;
    char* res = extract_spectrum_inten(
        static_cast<char*>(handle->GetMapping()),
        handle->GetDctx(),
        handle->GetDataFormat(),
        handle->GetIntenBlockLens(),
        static_cast<long>(handle->GetFooter()->inten_binary_pos),
        handle->GetDivisions(),
        static_cast<long>(index),
        &out_len,
        FALSE
    );

    if (res == nullptr) {
        Napi::Error::New(env, "getIntenBinaryMsz: extraction failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    data_format_t* df = handle->GetDataFormat();
    Napi::Value result;

    if (df->source_inten_fmt == _64d_) {
        size_t count = out_len / sizeof(double);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(double));
        std::memcpy(buf.Data(), res, count * sizeof(double));
        result = Napi::Float64Array::New(env, count, buf, 0);
    } else if (df->source_inten_fmt == _32f_) {
        size_t count = out_len / sizeof(float);
        auto buf = Napi::ArrayBuffer::New(env, count * sizeof(float));
        std::memcpy(buf.Data(), res, count * sizeof(float));
        result = Napi::Float32Array::New(env, count, buf, 0);
    } else {
        free(res);
        Napi::Error::New(env, "getIntenBinaryMsz: unsupported format").ThrowAsJavaScriptException();
        return env.Null();
    }

    free(res);
    return result;
}

// Get XML for MSZ file
Napi::Value GetXmlMsz(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "getXmlMsz: (FileHandle, index) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    FileHandle* handle = Napi::ObjectWrap<FileHandle>::Unwrap(info[0].As<Napi::Object>());
    size_t index = static_cast<size_t>(info[1].As<Napi::Number>().Int64Value());

    footer_t* footer = handle->GetFooter();
    size_t out_len = 0;

    char* res = extract_spectra(
        static_cast<char*>(handle->GetMapping()),
        handle->GetDctx(),
        handle->GetDataFormat(),
        handle->GetXmlBlockLens(),
        handle->GetMzBlockLens(),
        handle->GetIntenBlockLens(),
        static_cast<long>(footer->xml_pos),
        static_cast<long>(footer->mz_binary_pos),
        static_cast<long>(footer->inten_binary_pos),
        footer->mz_fmt,
        footer->inten_fmt,
        handle->GetDivisions(),
        static_cast<long>(index),
        &out_len
    );

    if (res == nullptr) {
        Napi::Error::New(env, "getXmlMsz: extraction failed").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::String result = Napi::String::New(env, res, out_len);
    free(res);
    return result;
}

} // namespace mscompress
