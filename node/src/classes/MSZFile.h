#ifndef MSZFILE_H
#define MSZFILE_H

#include <napi.h>
#include "../../include/export.h"

namespace mscompress {

class MSZFile : public Napi::ObjectWrap<MSZFile> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    MSZFile(const Napi::CallbackInfo& info);
    ~MSZFile();

private:
    // Instance methods
    Napi::Value GetAccessions(const Napi::CallbackInfo& info);
    Napi::Value GetMzBinary(const Napi::CallbackInfo& info);
    Napi::Value GetIntenBinary(const Napi::CallbackInfo& info);
    Napi::Value GetXml(const Napi::CallbackInfo& info);
    Napi::Value Decompress(const Napi::CallbackInfo& info);
    Napi::Value Extract(const Napi::CallbackInfo& info);
    Napi::Value ReadBinary(const Napi::CallbackInfo& info);
    Napi::Value Close(const Napi::CallbackInfo& info);
    
    // Instance data
    void* mmap_ptr_;
    size_t filesize_;
    int fd_;
    std::string filepath_;
    data_format_t* df_;
    footer_t* footer_;
    divisions_t* divisions_;
    division_t* positions_;
    ZSTD_DCtx* dctx_;
    block_len_queue_t* xml_block_lens_;
    block_len_queue_t* mz_binary_block_lens_;
    block_len_queue_t* inten_binary_block_lens_;
};

} // namespace mscompress

#endif
