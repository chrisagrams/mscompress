/**
 * NAPI wrapper for opening an MSZ payload embedded inside an MSZX (tar)
 * archive without extracting it to disk. Mirrors Python's
 * MSZFile.from_mszx by combining find_tar_entry + get_mapping_range to
 * produce a FileHandle whose mmap points at the entry's byte offset.
 */

#include "types.h"

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

// Forward declaration — registered in addon.cpp.
Napi::Value OpenMszFromArchive(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsString()) {
        Napi::TypeError::New(env,
            "openMszFromArchive: (archivePath: string, entryName: string) expected"
        ).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    std::string archivePath = info[0].As<Napi::String>().Utf8Value();
    std::string entryName = info[1].As<Napi::String>().Utf8Value();

    int fd = open_input_file(const_cast<char*>(archivePath.c_str()));
    if (fd < 0) {
        Napi::Error::New(env, "openMszFromArchive: failed to open " + archivePath)
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }

    int64_t offset = 0;
    size_t size = 0;
    if (find_tar_entry(fd, entryName.c_str(), &offset, &size) != 0) {
        close_file(fd);
        Napi::Error::New(env,
            "openMszFromArchive: entry '" + entryName +
            "' not found in archive or archive is not a valid USTAR tar"
        ).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    size_t pad = 0;
    size_t mapLength = 0;
    void* base = get_mapping_range(fd, offset, size, &pad, &mapLength);
    if (base == nullptr) {
        close_file(fd);
        Napi::Error::New(env,
            "openMszFromArchive: MSZX archive truncated or corrupted "
            "(mmap range exceeds file size)"
        ).ThrowAsJavaScriptException();
        return env.Undefined();
    }

    void* mapping = static_cast<char*>(base) + pad;

    // Build the descriptor object the FileHandle object-form constructor
    // expects, then construct via the registered FileHandle class. Pointer
    // values cross the JS boundary as External<void> wrappers.
    Napi::Object descriptor = Napi::Object::New(env);
    descriptor.Set("fd", Napi::Number::New(env, fd));
    descriptor.Set("mapping", Napi::External<void>::New(env, mapping));
    descriptor.Set("mapBase", Napi::External<void>::New(env, base));
    descriptor.Set("mapLength", Napi::Number::New(env, static_cast<double>(mapLength)));
    descriptor.Set("filesize", Napi::Number::New(env, static_cast<double>(size)));

    Napi::FunctionReference* ctor = env.GetInstanceData<Napi::FunctionReference>();
    if (ctor == nullptr) {
        // Cleanup before bailing — we own the mapping and fd at this point.
        remove_mapping(base, mapLength);
        close_file(fd);
        Napi::Error::New(env, "openMszFromArchive: FileHandle constructor unavailable")
            .ThrowAsJavaScriptException();
        return env.Undefined();
    }
    return ctor->New({ descriptor });
}

} // namespace mscompress
