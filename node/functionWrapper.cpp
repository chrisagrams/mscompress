#include <napi.h>
#include <iostream>
#include "export.h"
using namespace std;

namespace mscompress { 

    Napi::Number GetTime(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        double time = get_time();
        return Napi::Number::New(env, time);
    }

    Napi::Number GetNumThreads(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        int np = get_num_threads();
        return Napi::Number::New(env, np);
    }

    Napi::Number GetFilesize(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        // Check if at least one argument is passed
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "String expected as argument").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0);
        }

        // Get the path
        Napi::String pathValue = info[0].As<Napi::String>();
        std::string path = pathValue.Utf8Value();
        char* charPath = (char*)path.c_str(); // Convert to char*

        int fs = get_filesize(charPath);
        return Napi::Number::New(env, fs);
    }

    Napi::Number GetFileDescriptor(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        // Check if at least one argument is passed
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "String expected as argument").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0);
        }

        // Get the path
        Napi::String pathValue = info[0].As<Napi::String>();
        std::string path = pathValue.Utf8Value();
        char* charPath = (char*)path.c_str(); // Convert to char*

        int fd = open_file(charPath);
        return Napi::Number::New(env, fd);
    }

    Napi::Number CloseFileDescriptor(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        // Check if at least one argument is passed
        if (info.Length() < 1 || !info[0].IsNumber()) {
            Napi::TypeError::New(env, "Number expected as argument").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0);
        }

        int fd = info[0].As<Napi::Number>().Int32Value();

        int result = close_file(fd);
        return Napi::Number::New(env, result);
    }

    Napi::Number GetFileType(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsExternal()) {
            Napi::TypeError::New(env, "Expected arguments: (External<void>)").ThrowAsJavaScriptException();
            return Napi::Number::New(env, -1); // Returning a default value in case of error
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();

        int fileType = determine_filetype(mmap_ptr);

        return Napi::Number::New(env, fileType);
    }

    // Function to create mmap poniter and pass it to JS
    Napi::Value CreateMmapPointer(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        
        if (info.Length() < 1 || !info[0].IsNumber()) {
            Napi::TypeError::New(env, "Number argument expected").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0); // Returning a default value in case of error
        }

        int fd = info[0].As<Napi::Number>().Int32Value();

        void* mmap_ptr = get_mapping(fd);

        return Napi::External<void>::New(env, mmap_ptr);
    }

    // Function to retrieve a 512-byte chunk from a memory-mapped region. (for testing purposes)
    Napi::Value Get512BytesFromMmap(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 2 || !info[0].IsExternal() || !info[1].IsNumber()) {
            Napi::TypeError::New(env, "Expected arguments: (External<void>, Number)").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0); // Returning a default value in case of error
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();
        uint32_t offset = info[1].As<Napi::Number>().Uint32Value();

        // Create a buffer for 512 bytes.
        Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::New(env, 512);
        std::memcpy(buffer.Data(), static_cast<uint8_t*>(mmap_ptr) + offset, 512);

        return buffer;
    }

    Napi::Value GetAccessions(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsExternal()) {
            Napi::TypeError::New(env, "Expected arguments: (External<void>)").ThrowAsJavaScriptException();
            return env.Null();
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();

        int type = determine_filetype(mmap_ptr);

        data_format_t* df;
        if(type == COMPRESS) // mzML
            df = pattern_detect((char*)mmap_ptr);
        else if(type == DECOMPRESS) //msz
            df = get_header_df(mmap_ptr);
        else
        {
            Napi::Error::New(env, "Unsupported file provided.").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Object result = CreateDataFormatObject(env, df);

        return result;
    }

    Napi::Value GetPositions(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 3 || !info[0].IsExternal() || !info[1].IsObject() || !info[2].IsNumber()) {
            Napi::TypeError::New(env, "Expected arguments: (External<void>, Object, Number)").ThrowAsJavaScriptException();
            return env.Null();
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();
        data_format_t* df = NapiObjectToDataFormatT(info[1].As<Napi::Object>());
        long end = info[2].As<Napi::Number>().Int64Value();

        int type = determine_filetype(mmap_ptr);

        division_t* result = NULL;

        if(type == COMPRESS)
            result = find_binary_quick_w_spectra((char*)mmap_ptr, df, end);
        else if (type == DECOMPRESS)
        {
            Napi::Error::New(env, "Not yet implemented for msz files.").ThrowAsJavaScriptException();
            return env.Null();
        }

        if (result == NULL) {
            Napi::Error::New(env, "Error in GetPositions").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Object obj = CreateDivisionObject(env, result);

        free(df);

        return obj;
    }

}