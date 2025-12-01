#ifndef MZMLFILE_H
#define MZMLFILE_H

#include <napi.h>
#include "../../include/export.h"

namespace mscompress {

class MZMLFile : public Napi::ObjectWrap<MZMLFile> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    MZMLFile(const Napi::CallbackInfo& info);
    ~MZMLFile();

private:
    // Instance methods
    Napi::Value GetAccessions(const Napi::CallbackInfo& info);
    Napi::Value GetPositions(const Napi::CallbackInfo& info);
    Napi::Value DecodeBinary(const Napi::CallbackInfo& info);
    Napi::Value GetMzBinary(const Napi::CallbackInfo& info);
    Napi::Value GetIntenBinary(const Napi::CallbackInfo& info);
    Napi::Value GetXml(const Napi::CallbackInfo& info);
    Napi::Value Compress(const Napi::CallbackInfo& info);
    Napi::Value Extract(const Napi::CallbackInfo& info);
    Napi::Value Close(const Napi::CallbackInfo& info);
    
    // Instance data
    void* mmap_ptr_;
    size_t filesize_;
    int fd_;
    std::string filepath_;
    data_format_t* df_;
    division_t* division_;
};

} // namespace mscompress

#endif
