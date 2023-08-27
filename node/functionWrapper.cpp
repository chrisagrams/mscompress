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
            Napi::TypeError::New(env, "GetFilesize: string expected as argument").ThrowAsJavaScriptException();
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
            Napi::TypeError::New(env, "GetFileDescriptor: string expected as argument").ThrowAsJavaScriptException();
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
            Napi::TypeError::New(env, "CloseFileDescriptor number expected as argument").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0);
        }

        int fd = info[0].As<Napi::Number>().Int32Value();

        int result = close_file(fd);
        return Napi::Number::New(env, result);
    }

    Napi::Number GetFileType(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsExternal() || !info[1].IsNumber()) {
            Napi::TypeError::New(env, "GetFileType expected arguments: (External<void>, Number)").ThrowAsJavaScriptException();
            return Napi::Number::New(env, -1); // Returning a default value in case of error
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();

        size_t filesize = info[1].As<Napi::Number>().Int64Value();

        int fileType = determine_filetype(mmap_ptr, filesize);

        return Napi::Number::New(env, fileType);
    }

    // Function to create mmap poniter and pass it to JS
    Napi::Value CreateMmapPointer(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();
        
        if (info.Length() < 1 || !info[0].IsNumber()) {
            Napi::TypeError::New(env, "CreateMmapPointer number argument expected").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0); // Returning a default value in case of error
        }

        int fd = info[0].As<Napi::Number>().Int32Value();

        void* mmap_ptr = get_mapping(fd);

        return Napi::External<void>::New(env, mmap_ptr);
    }

    // Function to retrieve a chunk from a memory-mapped region.
    Napi::Value ReadFromFile(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 2 || !info[0].IsExternal() || !info[1].IsNumber() || !info[2].IsNumber()) {
            Napi::TypeError::New(env, "Get512BytesFromMmap expected arguments: (External<void>, Number, Number)").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0); // Returning a default value in case of error
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();
        uint32_t offset = info[1].As<Napi::Number>().Uint32Value();
        uint32_t len = info[2].As<Napi::Number>().Uint32Value();

        // Create a buffer for 512 bytes.
        Napi::Buffer<uint8_t> buffer = Napi::Buffer<uint8_t>::New(env, len);
        std::memcpy(buffer.Data(), static_cast<uint8_t*>(mmap_ptr) + offset, len);

        return buffer;
    }

    Napi::Value GetAccessions(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 1 || !info[0].IsExternal() || !info[1].IsNumber()) {
            Napi::TypeError::New(env, "GetAccessions expected arguments: (External<void>, Number)").ThrowAsJavaScriptException();
            return Napi::Number::New(env, -1); // Returning a default value in case of error
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();

        size_t filesize = info[1].As<Napi::Number>().Int64Value();

        int type = determine_filetype(mmap_ptr, filesize);

        data_format_t* df;
        if(type == COMPRESS) // mzML
            df = pattern_detect((char*)mmap_ptr);
        else if(type == DECOMPRESS) //msz
            df = get_header_df(mmap_ptr);
        else
        {
            Napi::Error::New(env, "GetAccessions: unsupported file provided.").ThrowAsJavaScriptException();
            return env.Null();
        }

        Napi::Object result = CreateDataFormatObject(env, df);

        return result;
    }

    Napi::Value GetPositions(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if (info.Length() < 3 || !info[0].IsExternal() || !info[1].IsNumber() || !info[2].IsObject()) {
            Napi::TypeError::New(env, "GetPositions expected arguments: (External<void>, Number, Object)").ThrowAsJavaScriptException();
            return env.Null();
        }

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();
        size_t filesize = info[1].As<Napi::Number>().Int64Value();
        data_format_t* df = NapiObjectToDataFormatT(info[2].As<Napi::Object>());

        int type = determine_filetype(mmap_ptr, filesize);

        division_t* result = NULL;

        if(type == COMPRESS)
            result = scan_mzml((char*)mmap_ptr, df, filesize, MSLEVEL|SCANNUM|RETTIME);
        else if (type == DECOMPRESS)
        {
            Napi::Error::New(env, "GetPositions: not yet implemented for msz files.").ThrowAsJavaScriptException();
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

    Napi::Value DecodeBinary(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if(info.Length() < 4 || !info[0].IsExternal() || !info[1].IsObject() || !info[2].IsNumber() || !info[3].IsNumber()) {
            Napi::TypeError::New(env, "DecodeBinary expected arguments: (External<void>, Object, Number, Number)").ThrowAsJavaScriptException();
            return env.Null();
        }

        std::cout << "DecodeBinary" << std::endl;

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();
        data_format_t* df = NapiObjectToDataFormatT(info[1].As<Napi::Object>());
        uint64_t start = info[2].As<Napi::Number>().Int64Value();
        uint64_t end = info[3].As<Napi::Number>().Int64Value();

        std::cout << "start: " << start << std::endl;
        std::cout << "end: " << end << std::endl;


        char* src = (char*)mmap_ptr + start;
        size_t src_len = end-start;
        size_t max_buffer_size = ((src_len + 3) / 4) * 3;
        char* dest = (char*)malloc(max_buffer_size);
        size_t out_len = 0;
        
        decode_base64(src, dest, src_len, &out_len);

        std::cout << "out_len: " << out_len << std::endl;

        switch(df->source_compression) {
            case _zlib_ :
                {
                    z_stream* z = alloc_z_stream();
                    zlib_block_t* decmp_output = zlib_alloc(0);
                    if(decmp_output == NULL) {
                        Napi::Error::New(env, "Error in zlib_alloc").ThrowAsJavaScriptException();
                        return env.Null();
                    }
                    ZLIB_TYPE decmp_size = (ZLIB_TYPE)zlib_decompress(z, (Bytef*)dest, decmp_output, out_len);
                    std::cout << "decmp_size: " << decmp_size << std::endl;
                    dest = (char*)decmp_output->buff;
                    out_len = decmp_size;
                    break;
                }
            case _no_comp_:
                {
                    // Shrink the buffer to the actual size needed
                    char* new_dest = (char*)realloc(dest, out_len);
                    if (new_dest == NULL) {
                        free(dest);
                        Napi::Error::New(env, "Error in realloc").ThrowAsJavaScriptException();
                        return env.Null();
                    }
                    dest = new_dest;
                    break;
                }
            
            default:
            {
                Napi::Error::New(env, "df does not contain proper source_compression").ThrowAsJavaScriptException();
                return env.Null();
            }
        }

        switch(df->source_mz_fmt) {
            case _64d_:
            {
                return DoubleArrayToNapiArray(env, (double*)dest, out_len/sizeof(double));
            }
            case _32f_:
            {
                return FloatArrayToNapiArray(env, (float*)dest, out_len/sizeof(float));
            }
            default:
            {
                Napi::Error::New(env, "df does not contain proper source_fmt").ThrowAsJavaScriptException();
                return env.Null();
            }
        }
    }

}