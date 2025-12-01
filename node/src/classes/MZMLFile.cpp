#include "MZMLFile.h"
#include "../utils/objectWrapper.h"
#include "../../include/progress.h"
#include <iostream>
#include <variant>
#include <vector>

namespace mscompress {

Napi::Object MZMLFile::Init(Napi::Env env, Napi::Object exports) {
    Napi::Function func = DefineClass(env, "MZMLFile", {
        InstanceMethod("getAccessions", &MZMLFile::GetAccessions),
        InstanceMethod("getPositions", &MZMLFile::GetPositions),
        InstanceMethod("decodeBinary", &MZMLFile::DecodeBinary),
        InstanceMethod("getMzBinary", &MZMLFile::GetMzBinary),
        InstanceMethod("getIntenBinary", &MZMLFile::GetIntenBinary),
        InstanceMethod("getXml", &MZMLFile::GetXml),
        InstanceMethod("compress", &MZMLFile::Compress),
        InstanceMethod("extract", &MZMLFile::Extract),
        InstanceMethod("close", &MZMLFile::Close)
    });

    exports.Set("MZMLFile", func);
    return exports;
}

MZMLFile::MZMLFile(const Napi::CallbackInfo& info) 
    : Napi::ObjectWrap<MZMLFile>(info), mmap_ptr_(nullptr), filesize_(0), fd_(-1), df_(nullptr), division_(nullptr) {
    
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String filepath expected").ThrowAsJavaScriptException();
        return;
    }

    filepath_ = info[0].As<Napi::String>().Utf8Value();
    
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

    // Detect data format pattern
    df_ = pattern_detect((char*)mmap_ptr_);
    
    if (df_ == nullptr) {
        close_file(fd_);
        fd_ = -1;
        Napi::Error::New(env, "Failed to detect mzML format").ThrowAsJavaScriptException();
        return;
    }
}

MZMLFile::~MZMLFile() {
    if (fd_ >= 0) {
        close_file(fd_);
    }
    if (df_ != nullptr) {
        delete df_;
    }
    if (division_ != nullptr) {
        // TODO: Free division memory properly
        delete division_;
    }
}

Napi::Value MZMLFile::GetAccessions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (df_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }
    
    return CreateDataFormatObject(env, df_);
}

Napi::Value MZMLFile::GetPositions(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (mmap_ptr_ == nullptr || df_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (division_ == nullptr) {
        std::cout << "Scanning mzML file..." << std::endl;
        division_ = scan_mzml((char*)mmap_ptr_, df_, filesize_, MSLEVEL|SCANNUM|RETTIME);
        if (division_ == nullptr) {
            Napi::Error::New(env, "Failed to scan mzML").ThrowAsJavaScriptException();
            return env.Null();
        }
        std::cout << "Scan complete." << std::endl;
    }

    return CreateDivisionObject(env, division_);
}

Napi::Value MZMLFile::GetMzBinary(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected spectrum index (Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || df_ == nullptr || division_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t index = info[0].As<Napi::Number>().Int64Value();
    
    if (index >= division_->mz->total_spec) {
        Napi::Error::New(env, "Index out of bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t start = division_->mz->start_positions[index];
    uint64_t end = division_->mz->end_positions[index];
    char* src = (char*)mmap_ptr_ + start;
    size_t src_len = end - start;
    
    try {
        auto result = decodeAndDecompress(df_->source_compression, df_->source_mz_fmt, src, src_len);
        if (std::holds_alternative<std::vector<double>>(result)) {
            std::vector<double>& vec = std::get<std::vector<double>>(result);
            return DoubleArrayToNapiArray(env, vec.data(), vec.size());
        } else {
            std::vector<float>& vec = std::get<std::vector<float>>(result);
            return FloatArrayToNapiArray(env, vec.data(), vec.size());
        }
    } catch (const std::runtime_error& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Value MZMLFile::GetIntenBinary(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected spectrum index (Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || df_ == nullptr || division_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t index = info[0].As<Napi::Number>().Int64Value();
    
    if (index >= division_->inten->total_spec) {
        Napi::Error::New(env, "Index out of bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t start = division_->inten->start_positions[index];
    uint64_t end = division_->inten->end_positions[index];
    char* src = (char*)mmap_ptr_ + start;
    size_t src_len = end - start;
    
    try {
        auto result = decodeAndDecompress(df_->source_compression, df_->source_inten_fmt, src, src_len);
        if (std::holds_alternative<std::vector<double>>(result)) {
            std::vector<double>& vec = std::get<std::vector<double>>(result);
            return DoubleArrayToNapiArray(env, vec.data(), vec.size());
        } else {
            std::vector<float>& vec = std::get<std::vector<float>>(result);
            return FloatArrayToNapiArray(env, vec.data(), vec.size());
        }
    } catch (const std::runtime_error& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Value MZMLFile::GetXml(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Expected spectrum index (Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || division_ == nullptr) {
        Napi::Error::New(env, "File not properly initialized").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t index = info[0].As<Napi::Number>().Int64Value();
    
    if (index >= division_->spectra->total_spec) {
        Napi::Error::New(env, "Index out of bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t start = division_->spectra->start_positions[index];
    uint64_t end = division_->spectra->end_positions[index];
    size_t size = end - start;

    if (start + size > filesize_) {
        Napi::Error::New(env, "Read beyond file bounds").ThrowAsJavaScriptException();
        return env.Null();
    }

    char* xml_data = (char*)mmap_ptr_ + start;
    
    return Napi::String::New(env, xml_data, size);
}

Napi::Value MZMLFile::DecodeBinary(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected (Number, Number)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr || df_ == nullptr) {
        Napi::Error::New(env, "File not open").ThrowAsJavaScriptException();
        return env.Null();
    }

    uint64_t start = info[0].As<Napi::Number>().Int64Value();
    uint64_t end = info[1].As<Napi::Number>().Int64Value();

    if (start >= filesize_ || end > filesize_ || start >= end) {
        Napi::Error::New(env, "Invalid byte range").ThrowAsJavaScriptException();
        return env.Null();
    }

    char* src = (char*)mmap_ptr_ + start;
    size_t src_len = end - start;
    
    try {
        auto result = decodeAndDecompress(df_->source_compression, df_->source_mz_fmt, src, src_len);
        if (std::holds_alternative<std::vector<double>>(result)) {
            std::vector<double>& vec = std::get<std::vector<double>>(result);
            return DoubleArrayToNapiArray(env, vec.data(), vec.size());
        } else {
            std::vector<float>& vec = std::get<std::vector<float>>(result);
            return FloatArrayToNapiArray(env, vec.data(), vec.size());
        }
    } catch (const std::runtime_error& e) {
        Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
        return env.Null();
    }
}

Napi::Value MZMLFile::Compress(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsString()) {
        Napi::TypeError::New(env, "Expected (Object, String)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr) {
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

    divisions_t* divisions;
    data_format_t* df_local = df_;
    
    // Determine file type
    int fileType = determine_filetype(mmap_ptr_, filesize_);
    
    if (fileType == COMPRESS) {
        // Standard mzML
        preprocess_mzml((char*)mmap_ptr_, filesize_, &(args->blocksize), args, &df_local, &divisions);
    } else if (fileType == EXTERNAL) {
        // External format
        preprocess_external((char*)mmap_ptr_, filesize_, &(args->blocksize), args, &df_local, &divisions);
    } else {
        close_file(output_fd);
        delete args;
        Napi::Error::New(env, "Invalid filetype for compression").ThrowAsJavaScriptException();
        return env.Null();
    }

    // Check for optional progress callback (3rd argument)
    ProgressData* progress_data = nullptr;
    if (info.Length() >= 3 && info[2].IsFunction()) {
        progress_data = new ProgressData();
        progress_data->tsfn = Napi::ThreadSafeFunction::New(
            env,
            info[2].As<Napi::Function>(),
            "MZMLFile Compress Progress",
            0,  // unlimited queue
            1   // initial thread count
        );
    }

    std::cout << "Starting compression..." << std::endl;
    std::cout << "threads: " << args->threads << std::endl;
    std::cout << "target_xml_format: " << args->target_xml_format << std::endl;
    std::cout << "target_mz_format: " << args->target_mz_format << std::endl;
    std::cout << "target_inten_format: " << args->target_inten_format << std::endl;
    std::cout << "zstd_compression_level: " << args->zstd_compression_level << std::endl;

    compress_mzml((char*)mmap_ptr_, filesize_, args, df_local, divisions, output_fd,
                 progress_data ? progress_callback_bridge : nullptr,
                 progress_data ? (void*)progress_data : nullptr);
    
    // Cleanup progress callback
    if (progress_data) {
        progress_data->tsfn.Release();
        delete progress_data;
    }

    std::cout << "Compression finished." << std::endl;
    
    close_file(output_fd);
    delete args;

    return env.Undefined();
}

Napi::Value MZMLFile::Extract(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsString()) {
        Napi::TypeError::New(env, "Expected (Object, String)").ThrowAsJavaScriptException();
        return env.Null();
    }

    if (mmap_ptr_ == nullptr) {
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

    divisions_t* divisions;
    data_format_t* df_local = df_;
    args->threads = -1; // Force single threaded
    
    preprocess_mzml((char*)mmap_ptr_, filesize_, &(args->blocksize), args, &df_local, &divisions);
    extract_mzml((char*)mmap_ptr_, divisions, output_fd);
    
    close_file(output_fd);
    delete args;

    return env.Undefined();
}

Napi::Value MZMLFile::Close(const Napi::CallbackInfo& info) {
    if (fd_ >= 0) {
        close_file(fd_);
        fd_ = -1;
    }
    mmap_ptr_ = nullptr;
    return info.Env().Undefined();
}

} // namespace mscompress
