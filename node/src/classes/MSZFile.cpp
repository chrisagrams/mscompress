#include "MSZFile.h"
#include "../utils/objectWrapper.h"
#include "../../include/progress.h"
#include <iostream>

namespace mscompress {

Napi::Object MSZFile::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "MSZFile", {
        InstanceMethod("getAccessions", &MSZFile::GetAccessions),
        InstanceMethod("getMzBinary", &MSZFile::GetMzBinary),
        InstanceMethod("getIntenBinary", &MSZFile::GetIntenBinary),
        InstanceMethod("getXml", &MSZFile::GetXml),
        InstanceMethod("decompress", &MSZFile::Decompress),
        InstanceMethod("extract", &MSZFile::Extract),
        InstanceMethod("readBinary", &MSZFile::ReadBinary),
        InstanceMethod("close", &MSZFile::Close)
    });

    exports.Set("MSZFile", func);
    return exports;
}

MSZFile::MSZFile(const Napi::CallbackInfo& info) 
    : Napi::ObjectWrap<MSZFile>(info), mmap_ptr_(nullptr), filesize_(0), fd_(-1), df_(nullptr),
      footer_(nullptr), divisions_(nullptr), positions_(nullptr), dctx_(nullptr),
      xml_block_lens_(nullptr), mz_binary_block_lens_(nullptr), inten_binary_block_lens_(nullptr) {
    
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String filepath expected").ThrowAsJavaScriptException();
        return;
    }

    filepath_ = info[0].As<Napi::String>().Utf8Value();
    
    // Open file and create mapping
    fd_ = open_input_file((char*)filepath_.c_str());
    if (fd_ < 0) {
        Napi::Error::New(env, "Failed to open file").ThrowAsJavaScriptException();
        return;
    }

    filesize_ = get_filesize((char*)filepath_.c_str());
    mmap_ptr_ = get_mapping(fd_);
    
    if (mmap_ptr_ == nullptr) {
        close_file(fd_);
        fd_ = -1;
        Napi::Error::New(env, "Failed to create memory mapping").ThrowAsJavaScriptException();
        return;
    }

    // Get data format
    df_ = get_header_df(mmap_ptr_);
    
    if (df_ == nullptr) {
        close_file(fd_);
        fd_ = -1;
        Napi::Error::New(env, "Failed to read file header").ThrowAsJavaScriptException();
        return;
    }

    // Read footer
    footer_ = read_footer(mmap_ptr_, filesize_);
    if (footer_ == nullptr) {
        close_file(fd_);
        fd_ = -1;
        Napi::Error::New(env, "Failed to read footer").ThrowAsJavaScriptException();
        return;
    }

    // Read divisions
    divisions_ = read_divisions(mmap_ptr_, footer_->divisions_t_pos, footer_->n_divisions);
    if (divisions_ == nullptr) {
        close_file(fd_);
        fd_ = -1;
        Napi::Error::New(env, "Failed to read divisions").ThrowAsJavaScriptException();
        return;
    }

    // Flatten divisions to positions
    positions_ = flatten_divisions(divisions_);

    // Allocate ZSTD decompression context
    dctx_ = alloc_dctx();
    
    // Read block length queues
    xml_block_lens_ = read_block_len_queue(mmap_ptr_, footer_->xml_blk_pos, footer_->mz_binary_blk_pos);
    mz_binary_block_lens_ = read_block_len_queue(mmap_ptr_, footer_->mz_binary_blk_pos, footer_->inten_binary_blk_pos);
    inten_binary_block_lens_ = read_block_len_queue(mmap_ptr_, footer_->inten_binary_blk_pos, footer_->divisions_t_pos);
    
    // Set runtime variables
    set_decompress_runtime_variables(df_, footer_);
}

MSZFile::~MSZFile() {
    if (fd_ >= 0) {
        close_file(fd_);
    }
    if (df_ != nullptr) {
        delete df_;
    }
    if (footer_ != nullptr) {
        delete footer_;
    }
    if (divisions_ != nullptr) {
        // Free divisions properly
        delete divisions_;
    }
    if (positions_ != nullptr) {
        delete positions_;
    }
    // ZSTD context cleanup handled by library
    // if (dctx_ != nullptr) {
    //     // dealloc_dctx(dctx_);
    // }
    if (xml_block_lens_ != nullptr) {
        delete xml_block_lens_;
    }
    if (mz_binary_block_lens_ != nullptr) {
        delete mz_binary_block_lens_;
    }
    if (inten_binary_block_lens_ != nullptr) {
        delete inten_binary_block_lens_;
    }
}

Napi::Value MSZFile::GetAccessions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (df_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    return CreateDataFormatObject(env, df_);
}

Napi::Value MSZFile::GetMzBinary(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected spectrum index (Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || dctx_ == nullptr || positions_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t index = info[0].As<Napi::Number>().Int64Value();
    
    if (index >= positions_->mz->total_spec) {
        Napi::Error::New(env, "Index out of bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    size_t out_len = 0;
    char* res = extract_spectrum_mz(
        (char*)mmap_ptr_, dctx_, df_, mz_binary_block_lens_,
        footer_->mz_binary_pos, divisions_, index, &out_len, 0
    );

    if (res == nullptr) {
        Napi::Error::New(env, "Failed to extract m/z binary").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Array result;
    if (df_->source_mz_fmt == 1000523) { // _64d_
        size_t count = out_len / 8;
        double* double_ptr = (double*)res;
        result = DoubleArrayToNapiArray(env, double_ptr, count);
    } else if (df_->source_mz_fmt == 1000521) { // _32f_
        size_t count = out_len / 4;
        float* float_ptr = (float*)res;
        result = FloatArrayToNapiArray(env, float_ptr, count);
    } else {
        free(res);
        Napi::Error::New(env, "Unsupported data format").ThrowAsJavaScriptException();
        return env.Null();
    }

    free(res);
    return result;
}

Napi::Value MSZFile::GetIntenBinary(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected spectrum index (Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || dctx_ == nullptr || positions_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t index = info[0].As<Napi::Number>().Int64Value();
    
    if (index >= positions_->inten->total_spec) {
        Napi::Error::New(env, "Index out of bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    size_t out_len = 0;
    char* res = extract_spectrum_inten(
        (char*)mmap_ptr_, dctx_, df_, inten_binary_block_lens_,
        footer_->inten_binary_pos, divisions_, index, &out_len, 0
    );

    if (res == nullptr) {
        Napi::Error::New(env, "Failed to extract intensity binary").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Array result;
    if (df_->source_inten_fmt == 1000523) { // _64d_
        size_t count = out_len / 8;
        double* double_ptr = (double*)res;
        result = DoubleArrayToNapiArray(env, double_ptr, count);
    } else if (df_->source_inten_fmt == 1000521) { // _32f_
        size_t count = out_len / 4;
        float* float_ptr = (float*)res;
        result = FloatArrayToNapiArray(env, float_ptr, count);
    } else {
        free(res);
        Napi::Error::New(env, "Unsupported data format").ThrowAsJavaScriptException();
        return env.Null();
    }

    free(res);
    return result;
}

Napi::Value MSZFile::GetXml(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected spectrum index (Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || dctx_ == nullptr || positions_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t index = info[0].As<Napi::Number>().Int64Value();
    
    if (index >= positions_->spectra->total_spec) {
        Napi::Error::New(env, "Index out of bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    size_t out_len = 0;
    char* res = extract_spectra(
        (char*)mmap_ptr_, dctx_, df_,
        xml_block_lens_, mz_binary_block_lens_, inten_binary_block_lens_,
        footer_->xml_pos, footer_->mz_binary_pos, footer_->inten_binary_pos,
        footer_->mz_fmt, footer_->inten_fmt,
        divisions_, index, &out_len
    );

    if (res == nullptr) {
        Napi::Error::New(env, "Failed to extract XML").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::String result = Napi::String::New(env, res, out_len);
    free(res);
    return result;
}

Napi::Value MSZFile::Decompress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsString()) {
        Napi::TypeError::New(env, "Expected (Object, String)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || fd_ < 0) {
        Napi::Error::New(env, "File not open").ThrowAsJavaScriptException();
        return env.Null();
    }

    Arguments* args = NapiObjectToArguments(info[0].As<Napi::Object>());
    std::string output_path = info[1].As<Napi::String>().Utf8Value();
    int output_fd = open_output_file((char*)output_path.c_str());

    if (output_fd < 0) {
        delete args;
        Napi::Error::New(env, "Failed to open output file").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Check for optional progress callback (3rd argument)
    ProgressData* progress_data = nullptr;
    if (info.Length() >= 3 && info[2].IsFunction()) {
        progress_data = new ProgressData();
        progress_data->tsfn = Napi::ThreadSafeFunction::New(
            env,
            info[2].As<Napi::Function>(),
            "MSZFile Decompress Progress",
            0,  // unlimited queue
            1   // initial thread count
        );
    }

    std::cout << "Starting decompression..." << std::endl;
    decompress_msz((char*)mmap_ptr_, filesize_, args, output_fd,
                  progress_data ? progress_callback_bridge : nullptr,
                  progress_data ? (void*)progress_data : nullptr);

    // Cleanup progress callback
    if (progress_data) {
        progress_data->tsfn.Release();
        delete progress_data;
    }

    std::cout << "Decompression finished." << std::endl;
    
    close_file(output_fd);
    delete args;

    return env.Undefined();
}

Napi::Value MSZFile::Extract(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsString()) {
        Napi::TypeError::New(env, "Expected (Object, String)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || fd_ < 0) {
        Napi::Error::New(env, "File not open").ThrowAsJavaScriptException();
        return env.Null();
    }

    Arguments* args = NapiObjectToArguments(info[0].As<Napi::Object>());
    std::string output_path = info[1].As<Napi::String>().Utf8Value();
    int output_fd = open_output_file((char*)output_path.c_str());

    if (output_fd < 0) {
        delete args;
        Napi::Error::New(env, "Failed to open output file").ThrowAsJavaScriptException();
        return env.Null();
    }

    extract_msz((char*)mmap_ptr_, filesize_, args->indices, args->indices_length,
                args->scans, args->scans_length, args->ms_level, output_fd);
    
    close_file(output_fd);
    delete args;

    return env.Undefined();
}

Napi::Value MSZFile::ReadBinary(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected (Number, Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr) {
        Napi::Error::New(env, "File not open").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint32_t offset = info[0].As<Napi::Number>().Uint32Value();
    uint32_t len = info[1].As<Napi::Number>().Uint32Value();

    if (offset + len > filesize_) {
        Napi::Error::New(env, "Read beyond file bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::New(env, len);
    std::memcpy(buffer.Data(), static_cast<uint8_t*>(mmap_ptr_) + offset, len);

    return buffer;
}

Napi::Value MSZFile::Close(const Napi::CallbackInfo& info) {
    if (fd_ >= 0) {
        close_file(fd_);
        fd_ = -1;
    }
    mmap_ptr_ = nullptr;
    return info.Env().Undefined();
}

} // namespace mscompress
