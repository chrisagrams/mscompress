#include <napi.h>
#include <iostream>
#include <variant>
#include <vector>
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

    Napi::Number GetOutputFileDescriptor(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        // Check if at least one argument is passed
        if (info.Length() < 1 || !info[0].IsString()) {
            Napi::TypeError::New(env, "GetOutputFileDescriptor: string expected as argument").ThrowAsJavaScriptException();
            return Napi::Number::New(env, 0);
        }
        // Get the path
        Napi::String pathValue = info[0].As<Napi::String>();
        std::string path = pathValue.Utf8Value();
        char* charPath = (char*)path.c_str(); // Convert to char*

        std::cout << "GetOutputFileDescriptor: " << charPath << std::endl;
        int fd = open_output_file(charPath);
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

    Napi::String GetZlibVersion(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        const char* version = zlibVersion();

        return Napi::String::New(env, version);
    }

    Napi::Value DecodeBinary(const Napi::CallbackInfo& info) {
        Napi::Env env = info.Env();

        if(info.Length() < 4 || !info[0].IsExternal() || !info[1].IsObject() || !info[2].IsNumber() || !info[3].IsNumber()) {
            Napi::TypeError::New(env, "DecodeBinary expected arguments: (External<void>, Object, Number, Number)").ThrowAsJavaScriptException();
            return env.Null();
        }

        // std::cout << "DecodeBinary" << std::endl;

        void* mmap_ptr = info[0].As<Napi::External<void>>().Data();
        data_format_t* df = NapiObjectToDataFormatT(info[1].As<Napi::Object>());
        uint64_t start = info[2].As<Napi::Number>().Int64Value();
        uint64_t end = info[3].As<Napi::Number>().Int64Value();

        // std::cout << "start: " << start << std::endl;
        // std::cout << "end: " << end << std::endl;


        char* src = (char*)mmap_ptr + start;
        size_t src_len = end-start;
        
        try {
            auto result = decodeAndDecompress(df->source_compression, df->source_mz_fmt, src, src_len);
            if(std::holds_alternative<std::vector<double>>(result)) {
                std::vector<double>& vec = std::get<std::vector<double>>(result);
                return DoubleArrayToNapiArray(env, vec.data(), vec.size());
            } else {
                std::vector<float>& vec = std::get<std::vector<float>>(result);
                return FloatArrayToNapiArray(env, vec.data(), vec.size());
            }
        } catch(const std::runtime_error& e) {
            Napi::Error::New(env, e.what()).ThrowAsJavaScriptException();
            return env.Null();
        }       

    }

    Napi::Value PrepareCompression(const Napi::CallbackInfo& info) 
    /* Need to receive:
        div, single encapsulating division
        df, Also need to populate this
        arguments

        Run:
            populate runtime fields in df
            create_divisions(div, n_divisions)

        Return: divisons, df
    */
    {
        Napi::Env env = info.Env();

        //TEMPORARY COMMENTED OUT
        if(info.Length() < 3 || !info[0].IsObject() || !info[1].IsObject() || !info[2].IsObject())
        {
            Napi::TypeError::New(env, "PrepareCompression expected arguments: (Object, Object, Object)").ThrowAsJavaScriptException();
            return env.Null();
        }


        // Parse div
        division_t* div = NapiObjectToDivisionT(info[0].As<Napi::Object>());

        // Parse df
        data_format_t* df = NapiObjectToDataFormatT(info[1].As<Napi::Object>());

        // Parse arguments
        struct Arguments* args = NapiObjectToArguments(info[2].As<Napi::Object>());

        //TODO : Parse arguments

        // Run
        int n_divisions = args->threads;
        divisions_t* divisions = create_divisions(div, n_divisions);

        std::cout << "PrepareCompression" << std::endl;

        Napi::Array jsDivisions = Napi::Array::New(env, n_divisions);
        for(int i = 0; i < n_divisions; i++)
        {
            Napi::Object obj = CreateDivisionObject(env, divisions->divisions[i]);
            jsDivisions[i] = obj;
        }


        // Return
        Napi::Object result = Napi::Object::New(env);
        result.Set("divisions", jsDivisions);
        result.Set("df", CreateDataFormatObject(env, df));

        return result;
    }

    Napi::Value Compress(const Napi::CallbackInfo& info)
    {
        /*
        Need to receive:
            char* input_map
            size_t input_filesize
            arguments
            df
            divisions
            output_fd
        */
        Napi::Env env = info.Env();

        if(info.Length() < 6 || !info[0].IsExternal() || !info[1].IsNumber() || !info[2].IsObject() || !info[3].IsObject() || !info[4].IsObject() || !info[5].IsNumber())
        {
            Napi::TypeError::New(env, "Compress expected arguments: (External<void>, Number, Object, Object, Object, Number)").ThrowAsJavaScriptException();
            return env.Null();
        }

        // Parse input_map
        void* input_map = info[0].As<Napi::External<void>>().Data();

        // Parse input_filesize
        size_t input_filesize = info[1].As<Napi::Number>().Int64Value();

        // Parse arguments
        struct Arguments* args = NapiObjectToArguments(info[2].As<Napi::Object>());

        // Parse df
        data_format_t* df = NapiObjectToDataFormatT(info[3].As<Napi::Object>());

        // Parse divisions (not working yet)
        // divisions_t* divisions = NapiObjectToDivisionsT(info[4].As<Napi::Object>());

        // For now, recompute divisions
        divisions_t* divisions;
        preprocess_mzml((char*)input_map,
                        input_filesize,
                        &(args->blocksize),
                        args,
                        &df,
                        &divisions);
        
        // Parse output_fd
        int output_fd = info[5].As<Napi::Number>().Int32Value();

        // Run
        std::cout << "Starting compression..." << std::endl;
        compress_mzml((char*)input_map, input_filesize, args, df, divisions, output_fd);
        std::cout << "Compression finished." << std::endl;

        return env.Null();
    }

    Napi::Value Decompress(const Napi::CallbackInfo& info)
    {
        /*
            Need to receive:
            input_map
            input_filesize
            arguments
            output_fd
        */

        Napi::Env env = info.Env();

        if(info.Length() < 4 || !info[0].IsExternal() || !info[1].IsNumber() || !info[2].IsObject() || !info[3].IsNumber())
        {
            Napi::TypeError::New(env, "Decompress expected arguments: (External<void>, Number, Object, Number)").ThrowAsJavaScriptException();
            return env.Null();
        }
        // Parse input_map
        void* input_map = info[0].As<Napi::External<void>>().Data();

        // Parse input_filesize
        size_t input_filesize = info[1].As<Napi::Number>().Int64Value();

        // Parse arguments
        struct Arguments* args = NapiObjectToArguments(info[2].As<Napi::Object>());

        // Parse output_fd
        int output_fd = info[3].As<Napi::Number>().Int32Value();

        // Run
        std::cout << "Starting decompression..." << std::endl;
        decompress_msz((char*)input_map, input_filesize, args, output_fd);
        std::cout << "Decompression finished." << std::endl;

        return env.Null();
    }

}