#include "types.h"
#include <cstring>

namespace mscompress {

std::unordered_map<uint32_t, std::string> accession_to_string_map = {
    {1000519, "MS:1000519"}, // 32-bit integer
    {1000520, "MS:1000520"}, // 16-bit float
    {1000521, "MS:1000521"}, // 32-bit float
    {1000522, "MS:1000522"}, // 64-bit integer
    {1000523, "MS:1000523"}, // 64-bit double
    {1000574, "MS:1000574"}, // zlib compression
    {1000576, "MS:1000576"}, // no compression
    {1000515, "MS:1000515"}, // intensity array
    {1000514, "MS:1000514"}, // m/z array
    {1000513, "MS:1000513"}, // xml
    {4700000, "MS:4700000"}, // lossless
    {4700001, "MS:4700001"}, // ZSTD compression
    {4700002, "MS:4700002"}, // cast 64 to 32
    {4700003, "MS:4700003"}, // log2 transform
    {4700004, "MS:4700004"}, // delta16 transform
    {4700005, "MS:4700005"}, // delta24 transform
    {4700006, "MS:4700006"}, // delta32 transform
    {4700007, "MS:4700007"}, // vbr
    {4700008, "MS:4700008"}, // bitpack
    {4700009, "MS:4700009"}, // vdelta16 transform
    {4700010, "MS:4700010"}, // vdelta24 transform
    {4700011, "MS:4700011"}, // cast 64 to 16
    {4700012, "MS:4700012"}, // LZ4 compression / no_encode
};

std::string AccessionToString(uint32_t accession) {
    auto it = accession_to_string_map.find(accession);
    if (it != accession_to_string_map.end()) {
        return it->second;
    }
    return "MS:" + std::to_string(accession);
}

uint32_t StringToAccession(const std::string& str) {
    // Handle "MS:XXXXXX" format
    if (str.substr(0, 3) == "MS:") {
        try {
            return static_cast<uint32_t>(std::stoul(str.substr(3)));
        } catch (...) {
            return 0;
        }
    }
    // Try direct numeric parse
    try {
        return static_cast<uint32_t>(std::stoul(str));
    } catch (...) {
        return 0;
    }
}

// Helper functions
uint32_t getUint32OrDefault(const Napi::Object& obj, const std::string& key, uint32_t defaultValue) {
    if (obj.Has(key) && obj.Get(key).IsNumber()) {
        return obj.Get(key).As<Napi::Number>().Uint32Value();
    }
    return defaultValue;
}

int64_t getInt64OrDefault(const Napi::Object& obj, const std::string& key, int64_t defaultValue) {
    if (obj.Has(key) && obj.Get(key).IsNumber()) {
        return obj.Get(key).As<Napi::Number>().Int64Value();
    }
    return defaultValue;
}

float getFloatOrDefault(const Napi::Object& obj, const std::string& key, float defaultValue) {
    if (obj.Has(key) && obj.Get(key).IsNumber()) {
        return obj.Get(key).As<Napi::Number>().FloatValue();
    }
    return defaultValue;
}

std::string getStringOrDefault(const Napi::Object& obj, const std::string& key, const std::string& defaultValue) {
    if (obj.Has(key) && obj.Get(key).IsString()) {
        return obj.Get(key).As<Napi::String>().Utf8Value();
    }
    return defaultValue;
}

// C -> Napi conversions
Napi::Object CreateDataFormatObject(const Napi::Env& env, data_format_t* df) {
    Napi::Object obj = Napi::Object::New(env);
    obj.Set("sourceMzFmt", Napi::Number::New(env, df->source_mz_fmt));
    obj.Set("sourceIntenFmt", Napi::Number::New(env, df->source_inten_fmt));
    obj.Set("sourceCompression", Napi::Number::New(env, df->source_compression));
    obj.Set("sourceTotalSpec", Napi::Number::New(env, df->source_total_spec));
    obj.Set("targetXmlFormat", Napi::Number::New(env, df->target_xml_format));
    obj.Set("targetMzFormat", Napi::Number::New(env, df->target_mz_format));
    obj.Set("targetIntenFormat", Napi::Number::New(env, df->target_inten_format));
    obj.Set("mzScaleFactor", Napi::Number::New(env, df->mz_scale_factor));
    obj.Set("intScaleFactor", Napi::Number::New(env, df->int_scale_factor));
    return obj;
}

Napi::Object CreateDataPositionsObject(const Napi::Env& env, data_positions_t* dp) {
    Napi::Object obj = Napi::Object::New(env);
    if (dp == nullptr) return obj;

    int total = dp->total_spec;
    obj.Set("totalSpec", Napi::Number::New(env, total));

    // Use TypedArrays for efficiency
    auto startBuf = Napi::ArrayBuffer::New(env, total * sizeof(double));
    auto endBuf = Napi::ArrayBuffer::New(env, total * sizeof(double));
    auto startArr = Napi::Float64Array::New(env, total, startBuf, 0);
    auto endArr = Napi::Float64Array::New(env, total, endBuf, 0);

    for (int i = 0; i < total; i++) {
        startArr[i] = static_cast<double>(dp->start_positions[i]);
        endArr[i] = static_cast<double>(dp->end_positions[i]);
    }

    obj.Set("startPositions", startArr);
    obj.Set("endPositions", endArr);

    return obj;
}

Napi::Object CreateDivisionObject(const Napi::Env& env, division_t* division) {
    Napi::Object obj = Napi::Object::New(env);
    if (division == nullptr) return obj;

    obj.Set("spectra", CreateDataPositionsObject(env, division->spectra));
    obj.Set("xml", CreateDataPositionsObject(env, division->xml));
    obj.Set("mz", CreateDataPositionsObject(env, division->mz));
    obj.Set("inten", CreateDataPositionsObject(env, division->inten));
    obj.Set("size", Napi::Number::New(env, static_cast<double>(division->size)));

    int total = division->mz ? division->mz->total_spec : 0;

    // Scans as Uint32Array
    auto scansBuf = Napi::ArrayBuffer::New(env, total * sizeof(uint32_t));
    auto scansArr = Napi::Uint32Array::New(env, total, scansBuf, 0);
    if (division->scans) {
        std::memcpy(scansBuf.Data(), division->scans, total * sizeof(uint32_t));
    }
    obj.Set("scans", scansArr);

    // MS levels as Uint16Array
    auto msLevelsBuf = Napi::ArrayBuffer::New(env, total * sizeof(uint16_t));
    auto msLevelsArr = Napi::Uint16Array::New(env, total, msLevelsBuf, 0);
    if (division->ms_levels) {
        std::memcpy(msLevelsBuf.Data(), division->ms_levels, total * sizeof(uint16_t));
    }
    obj.Set("msLevels", msLevelsArr);

    // Retention times as Float32Array
    auto retTimesBuf = Napi::ArrayBuffer::New(env, total * sizeof(float));
    auto retTimesArr = Napi::Float32Array::New(env, total, retTimesBuf, 0);
    if (division->ret_times) {
        std::memcpy(retTimesBuf.Data(), division->ret_times, total * sizeof(float));
    }
    obj.Set("retTimes", retTimesArr);

    return obj;
}

Napi::Object CreateFooterObject(const Napi::Env& env, footer_t* footer) {
    Napi::Object obj = Napi::Object::New(env);
    if (footer == nullptr) return obj;

    obj.Set("xmlPos", Napi::Number::New(env, static_cast<double>(footer->xml_pos)));
    obj.Set("mzBinaryPos", Napi::Number::New(env, static_cast<double>(footer->mz_binary_pos)));
    obj.Set("intenBinaryPos", Napi::Number::New(env, static_cast<double>(footer->inten_binary_pos)));
    obj.Set("xmlBlkPos", Napi::Number::New(env, static_cast<double>(footer->xml_blk_pos)));
    obj.Set("mzBinaryBlkPos", Napi::Number::New(env, static_cast<double>(footer->mz_binary_blk_pos)));
    obj.Set("intenBinaryBlkPos", Napi::Number::New(env, static_cast<double>(footer->inten_binary_blk_pos)));
    obj.Set("divisionsTPos", Napi::Number::New(env, static_cast<double>(footer->divisions_t_pos)));
    obj.Set("numSpectra", Napi::Number::New(env, static_cast<double>(footer->num_spectra)));
    obj.Set("originalFilesize", Napi::Number::New(env, static_cast<double>(footer->original_filesize)));
    obj.Set("nDivisions", Napi::Number::New(env, footer->n_divisions));
    obj.Set("mzFmt", Napi::Number::New(env, footer->mz_fmt));
    obj.Set("intenFmt", Napi::Number::New(env, footer->inten_fmt));

    return obj;
}

// Napi -> C conversions
Arguments* NapiObjectToArguments(const Napi::Env& env, const Napi::Object& obj) {
    Arguments* args = new Arguments();
    init_args(args);

    args->threads = static_cast<int>(getUint32OrDefault(obj, "threads", static_cast<uint32_t>(get_num_threads())));
    args->blocksize = static_cast<long>(getInt64OrDefault(obj, "blocksize", 100000000L));
    args->mz_scale_factor = getFloatOrDefault(obj, "mzScaleFactor", 1000.0f);
    args->int_scale_factor = getFloatOrDefault(obj, "intScaleFactor", 0.0f);
    args->target_xml_format = static_cast<int>(getUint32OrDefault(obj, "targetXmlFormat", _ZSTD_compression_));
    args->target_mz_format = static_cast<int>(getUint32OrDefault(obj, "targetMzFormat", _ZSTD_compression_));
    args->target_inten_format = static_cast<int>(getUint32OrDefault(obj, "targetIntenFormat", _ZSTD_compression_));
    args->zstd_compression_level = static_cast<int>(getUint32OrDefault(obj, "zstdCompressionLevel", 3));
    args->ms_level = static_cast<long>(getInt64OrDefault(obj, "msLevel", 0));

    // Parse indices array
    if (obj.Has("indices") && obj.Get("indices").IsArray()) {
        Napi::Array indicesArr = obj.Get("indices").As<Napi::Array>();
        args->indices_length = static_cast<long>(indicesArr.Length());
        args->indices = new long[args->indices_length];
        for (uint32_t i = 0; i < indicesArr.Length(); i++) {
            args->indices[i] = static_cast<long>(indicesArr.Get(i).As<Napi::Number>().Int64Value());
        }
    }

    // Parse scans array
    if (obj.Has("scans") && obj.Get("scans").IsArray()) {
        Napi::Array scansArr = obj.Get("scans").As<Napi::Array>();
        args->scans_length = static_cast<long>(scansArr.Length());
        args->scans = new uint32_t[args->scans_length];
        for (uint32_t i = 0; i < static_cast<uint32_t>(args->scans_length); i++) {
            args->scans[i] = scansArr.Get(i).As<Napi::Number>().Uint32Value();
        }
    }

    return args;
}

// FileHandle implementation
Napi::Object FileHandle::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "FileHandle", {
        InstanceMethod("close", &FileHandle::Close),
        InstanceAccessor("filesize", &FileHandle::GetFilesizeValue, nullptr),
        InstanceAccessor("filetype", &FileHandle::GetFiletypeValue, nullptr),
        InstanceAccessor("isOpen", &FileHandle::IsOpenValue, nullptr),
    });

    Napi::FunctionReference* constructor = new Napi::FunctionReference();
    *constructor = Napi::Persistent(func);
    env.SetInstanceData(constructor);

    exports.Set("FileHandle", func);
    return exports;
}

FileHandle::FileHandle(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<FileHandle>(info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1) {
        Napi::TypeError::New(env, "FileHandle: path string or handle object expected").ThrowAsJavaScriptException();
        return;
    }

    // --- Object form: pre-built handle from offset-mmap (e.g. MSZX path) ---
    if (info[0].IsObject() && !info[0].IsString()) {
        Napi::Object obj = info[0].As<Napi::Object>();
        if (!obj.Has("fd") || !obj.Has("mapping") || !obj.Has("mapBase")
            || !obj.Has("mapLength") || !obj.Has("filesize")) {
            Napi::TypeError::New(env,
                "FileHandle: handle object must have {fd, mapping, mapBase, mapLength, filesize}"
            ).ThrowAsJavaScriptException();
            return;
        }

        fd_ = obj.Get("fd").As<Napi::Number>().Int32Value();
        // Pointers come across as External<void> wrappers (set by the addon side).
        mapping_ = obj.Get("mapping").As<Napi::External<void>>().Data();
        map_base_ = obj.Get("mapBase").As<Napi::External<void>>().Data();
        map_length_ = static_cast<size_t>(obj.Get("mapLength").As<Napi::Number>().Int64Value());
        filesize_ = static_cast<size_t>(obj.Get("filesize").As<Napi::Number>().Int64Value());

        filetype_ = determine_filetype(mapping_, filesize_);
        z_ = alloc_z_stream();
        return;
    }

    // --- String form: open and mmap from a file path ---
    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "FileHandle: string path expected").ThrowAsJavaScriptException();
        return;
    }

    std::string path = info[0].As<Napi::String>().Utf8Value();

    filesize_ = get_filesize(const_cast<char*>(path.c_str()));
    if (filesize_ == 0) {
        Napi::Error::New(env, "FileHandle: file is empty or does not exist: " + path).ThrowAsJavaScriptException();
        return;
    }

    fd_ = open_input_file(const_cast<char*>(path.c_str()));
    if (fd_ < 0) {
        Napi::Error::New(env, "FileHandle: failed to open file: " + path).ThrowAsJavaScriptException();
        return;
    }

    mapping_ = get_mapping(fd_);
    if (mapping_ == nullptr) {
        close_file(fd_);
        fd_ = -1;
        Napi::Error::New(env, "FileHandle: failed to memory map file: " + path).ThrowAsJavaScriptException();
        return;
    }

    // Path-based open: the user-facing pointer IS the mmap base.
    map_base_ = mapping_;
    map_length_ = filesize_;

    filetype_ = determine_filetype(mapping_, filesize_);
    z_ = alloc_z_stream();
}

FileHandle::~FileHandle() {
    Cleanup();
}

void FileHandle::Cleanup() {
    // Free ZSTD decompression context
    if (dctx_) {
        ZSTD_freeDCtx(dctx_);
        dctx_ = nullptr;
    }

    // Free block length queues
    if (xml_block_lens_) {
        dealloc_block_len_queue(xml_block_lens_);
        xml_block_lens_ = nullptr;
    }
    if (mz_block_lens_) {
        dealloc_block_len_queue(mz_block_lens_);
        mz_block_lens_ = nullptr;
    }
    if (inten_block_lens_) {
        dealloc_block_len_queue(inten_block_lens_);
        inten_block_lens_ = nullptr;
    }

    // Free divisions
    if (divisions_) {
        if (mmap_backed_divisions_) {
            dealloc_read_divisions(divisions_);
        } else {
            dealloc_divisions(divisions_);
        }
        divisions_ = nullptr;
    }

    // Free positions (always fully malloc'd)
    if (positions_) {
        dealloc_division(positions_);
        positions_ = nullptr;
    }

    // Free data format
    if (df_) {
        dealloc_df(df_);
        df_ = nullptr;
    }

    // Free z_stream
    if (z_) {
        dealloc_z_stream(z_);
        z_ = nullptr;
    }

    // footer_ points to mmap memory in MSZ path, don't free
    footer_ = nullptr;

    // Unmap then close. For offset-mmap (MSZX), unmap the true base + actual
    // mapped length, not the user-facing pointer.
    if (map_base_) {
        remove_mapping(map_base_, map_length_);
        map_base_ = nullptr;
        mapping_ = nullptr;
    } else if (mapping_) {
        remove_mapping(mapping_, filesize_);
        mapping_ = nullptr;
    }

    if (fd_ >= 0) {
        close_file(fd_);
        fd_ = -1;
    }
}

Napi::Value FileHandle::Close(const Napi::CallbackInfo& info) {
    Cleanup();
    return info.Env().Undefined();
}

Napi::Value FileHandle::GetFilesizeValue(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), static_cast<double>(filesize_));
}

Napi::Value FileHandle::GetFiletypeValue(const Napi::CallbackInfo& info) {
    return Napi::Number::New(info.Env(), filetype_);
}

Napi::Value FileHandle::IsOpenValue(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), IsOpen());
}

} // namespace mscompress
