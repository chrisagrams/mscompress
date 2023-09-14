#include <napi.h>
#include <variant>
#include <vector>
#include "mscompress.h"

using namespace std;

namespace mscompress
{
    // processing.cpp (internal, not exported)
    std::variant<std::vector<float>, std::vector<double>> decodeAndDecompress(
        int source_compression,
        int source_format,
        char* src,
        size_t src_len
    );

    // Object Wraps
    Napi::Array Uint64ArrayToNapiArray(const Napi::Env & env, uint64_t* arr, uint64_t size);
    void NapiArrayToUint64Array(const Napi::Env& env, const Napi::Array& jsArr, uint64_t* arr, uint64_t size);
    Napi::Array LongArrayToNapiArray(const Napi::Env& env, long* arr, uint64_t size);
    void NapiArrayToLongArray(const Napi::Env& env, const Napi::Array& jsArr, long* arr, uint64_t size);
    Napi::Array FloatArrayToNapiArray(const Napi::Env& env, float* arr, uint64_t size);
    void NapiArrayToFloatArray(const Napi::Env& env, const Napi::Array& jsArr, float* arr, uint64_t size);
    Napi::Array DoubleArrayToNapiArray(const Napi::Env& env, double* arr, uint64_t size);
    void NapiArrayToDoubleArray(const Napi::Env& env, const Napi::Array& jsArr, double* arr, uint64_t size);
    uint32_t getUint32OrDefault(const Napi::Object& obj, const std::string& key, uint32_t defaultValue);
    float getFloatOrDefault(const Napi::Object& obj, const std::string& key, float defaultValue);
    Napi::Object CreateDataFormatObject(const Napi::Env& env, data_format_t* df);
    data_format_t* NapiObjectToDataFormatT(const Napi::Object& obj);
    Napi::Object CreateDataPositionsObject(const Napi::Env& env, data_positions_t* dp);
    Napi::Object CreateDivisionObject(const Napi::Env& env, division_t* division);
    division_t* NapiObjectToDivisionT(const Napi::Object& obj);
    Arguments* NapiObjectToArguments(const Napi::Object& obj);

    // Function Wraps
    Napi::Number GetTime(const Napi::CallbackInfo& info);
    Napi::Number GetNumThreads(const Napi::CallbackInfo& info);
    Napi::Number GetFilesize(const Napi::CallbackInfo& info);
    Napi::Number GetFileDescriptor(const Napi::CallbackInfo& info);
    Napi::Number CloseFileDescriptor(const Napi::CallbackInfo& info);
    Napi::Number GetFileType(const Napi::CallbackInfo& info);
    Napi::Value CreateMmapPointer(const Napi::CallbackInfo& info);
    Napi::Value ReadFromFile(const Napi::CallbackInfo& info);
    Napi::Value GetAccessions(const Napi::CallbackInfo& info);
    Napi::Value GetPositions(const Napi::CallbackInfo& info);
    Napi::Value DecodeBinary(const Napi::CallbackInfo& info);
    Napi::String GetZlibVersion(const Napi::CallbackInfo& info);
    Napi::Value PrepareCompression(const Napi::CallbackInfo& info);

    //Export API
    Napi::Object Init(Napi::Env env, Napi::Object exports);
}
