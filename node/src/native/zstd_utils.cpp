#include "types.h"

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

// Compress a Buffer with zstd, returns a Buffer
Napi::Value ZstdCompress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBuffer()) {
        Napi::TypeError::New(env, "zstdCompress: (Buffer, level?) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto inputBuf = info[0].As<Napi::Buffer<char>>();
    const char* src = inputBuf.Data();
    size_t srcSize = inputBuf.Length();

    int level = ZSTD_CLEVEL_DEFAULT;
    if (info.Length() >= 2 && info[1].IsNumber()) {
        level = info[1].As<Napi::Number>().Int32Value();
    }

    size_t dstCapacity = ZSTD_compressBound(srcSize);
    auto outputBuf = Napi::Buffer<char>::New(env, dstCapacity);

    size_t compressedSize = ZSTD_compress(outputBuf.Data(), dstCapacity, src, srcSize, level);

    if (ZSTD_isError(compressedSize)) {
        Napi::Error::New(env, std::string("zstdCompress failed: ") + ZSTD_getErrorName(compressedSize))
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    // Return a correctly-sized copy
    return Napi::Buffer<char>::Copy(env, outputBuf.Data(), compressedSize);
}

// Decompress a zstd-compressed Buffer, returns a Buffer
Napi::Value ZstdDecompress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsBuffer()) {
        Napi::TypeError::New(env, "zstdDecompress: (Buffer) expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto inputBuf = info[0].As<Napi::Buffer<char>>();
    const char* src = inputBuf.Data();
    size_t srcSize = inputBuf.Length();

    unsigned long long decompressedSize = ZSTD_getFrameContentSize(src, srcSize);

    if (decompressedSize == ZSTD_CONTENTSIZE_UNKNOWN || decompressedSize == ZSTD_CONTENTSIZE_ERROR) {
        Napi::Error::New(env, "zstdDecompress: cannot determine decompressed size").ThrowAsJavaScriptException();
        return env.Null();
    }

    auto outputBuf = Napi::Buffer<char>::New(env, static_cast<size_t>(decompressedSize));

    size_t result = ZSTD_decompress(outputBuf.Data(), static_cast<size_t>(decompressedSize), src, srcSize);

    if (ZSTD_isError(result)) {
        Napi::Error::New(env, std::string("zstdDecompress failed: ") + ZSTD_getErrorName(result))
            .ThrowAsJavaScriptException();
        return env.Null();
    }

    return outputBuf;
}

} // namespace mscompress
