#ifndef MSCOMPRESS_NODE_TYPES_H
#define MSCOMPRESS_NODE_TYPES_H

#include <napi.h>
#include <unordered_map>
#include <string>

extern "C" {
#include "mscompress.h"
}

namespace mscompress {

// Accession string <-> uint32 mapping
extern std::unordered_map<uint32_t, std::string> accession_to_string_map;

std::string AccessionToString(uint32_t accession);
uint32_t StringToAccession(const std::string& str);

// C -> Napi conversions
Napi::Object CreateDataFormatObject(const Napi::Env& env, data_format_t* df);
Napi::Object CreateDataPositionsObject(const Napi::Env& env, data_positions_t* dp);
Napi::Object CreateDivisionObject(const Napi::Env& env, division_t* division);
Napi::Object CreateFooterObject(const Napi::Env& env, footer_t* footer);

// Napi -> C conversions
Arguments* NapiObjectToArguments(const Napi::Env& env, const Napi::Object& obj);

// Helper: get typed value from object with default
uint32_t getUint32OrDefault(const Napi::Object& obj, const std::string& key, uint32_t defaultValue);
int64_t getInt64OrDefault(const Napi::Object& obj, const std::string& key, int64_t defaultValue);
float getFloatOrDefault(const Napi::Object& obj, const std::string& key, float defaultValue);
std::string getStringOrDefault(const Napi::Object& obj, const std::string& key, const std::string& defaultValue);

// RAII FileHandle class
class FileHandle : public Napi::ObjectWrap<FileHandle> {
public:
    static Napi::Object Init(Napi::Env env, Napi::Object exports);
    FileHandle(const Napi::CallbackInfo& info);
    ~FileHandle();

    // Accessors
    void* GetMapping() const { return mapping_; }
    int GetFd() const { return fd_; }
    size_t GetFilesize() const { return filesize_; }
    int GetFiletype() const { return filetype_; }

    // For MSZ files: parsed state
    data_format_t* GetDataFormat() const { return df_; }
    void SetDataFormat(data_format_t* df) { df_ = df; }

    division_t* GetPositions() const { return positions_; }
    void SetPositions(division_t* pos) { positions_ = pos; }

    divisions_t* GetDivisions() const { return divisions_; }
    void SetDivisions(divisions_t* divs) { divisions_ = divs; }

    footer_t* GetFooter() const { return footer_; }
    void SetFooter(footer_t* f) { footer_ = f; }

    block_len_queue_t* GetXmlBlockLens() const { return xml_block_lens_; }
    void SetXmlBlockLens(block_len_queue_t* q) { xml_block_lens_ = q; }

    block_len_queue_t* GetMzBlockLens() const { return mz_block_lens_; }
    void SetMzBlockLens(block_len_queue_t* q) { mz_block_lens_ = q; }

    block_len_queue_t* GetIntenBlockLens() const { return inten_block_lens_; }
    void SetIntenBlockLens(block_len_queue_t* q) { inten_block_lens_ = q; }

    ZSTD_DCtx* GetDctx() const { return dctx_; }
    void SetDctx(ZSTD_DCtx* d) { dctx_ = d; }

    z_stream* GetZStream() const { return z_; }

    // Is file still open?
    bool IsOpen() const { return fd_ >= 0 && mapping_ != nullptr; }

    // Mark as mmap-backed divisions (MSZ read path)
    void SetMmapBacked(bool v) { mmap_backed_divisions_ = v; }
    bool IsMmapBacked() const { return mmap_backed_divisions_; }

private:
    Napi::Value Close(const Napi::CallbackInfo& info);
    Napi::Value GetFilesizeValue(const Napi::CallbackInfo& info);
    Napi::Value GetFiletypeValue(const Napi::CallbackInfo& info);
    Napi::Value IsOpenValue(const Napi::CallbackInfo& info);

    void Cleanup();

    int fd_ = -1;
    void* mapping_ = nullptr;          // user-facing pointer (== map_base_ + pad)
    void* map_base_ = nullptr;         // true mmap base, used for unmap
    size_t map_length_ = 0;            // true mapped length (≥ filesize_)
    size_t filesize_ = 0;              // logical MSZ size
    int filetype_ = 0;
    data_format_t* df_ = nullptr;
    division_t* positions_ = nullptr;
    divisions_t* divisions_ = nullptr;
    footer_t* footer_ = nullptr;
    block_len_queue_t* xml_block_lens_ = nullptr;
    block_len_queue_t* mz_block_lens_ = nullptr;
    block_len_queue_t* inten_block_lens_ = nullptr;
    ZSTD_DCtx* dctx_ = nullptr;
    z_stream* z_ = nullptr;
    bool mmap_backed_divisions_ = false;
};

} // namespace mscompress

#endif // MSCOMPRESS_NODE_TYPES_H
