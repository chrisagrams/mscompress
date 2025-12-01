#ifndef OBJECTWRAPPER_H
#define OBJECTWRAPPER_H

#include <napi.h>
#include <string>
#include "../../include/export.h"


namespace mscompress {

    // Accession conversion
    std::string AccessionToString(uint32_t accession);
    uint32_t StringToAccession(const std::string& str);

    // Array conversions
    Napi::Array Uint64ArrayToNapiArray(const Napi::Env & env, uint64_t* arr, uint64_t size);
    void NapiArrayToUint64Array(const Napi::Env& env, const Napi::Array& jsArr, uint64_t* arr, uint64_t size);
    
    Napi::Array Uint32ArrayToNapiArray(const Napi::Env & env, uint32_t* arr, uint64_t size);
    void NapiArrayToUint32Array(const Napi::Env& env, const Napi::Array& jsArr, uint32_t* arr, uint64_t size);
    
    Napi::Array Uint16ArrayToNapiArray(const Napi::Env & env, uint16_t* arr, uint64_t size);
    void NapiArrayToUint16Array(const Napi::Env& env, const Napi::Array& jsArr, uint16_t* arr, uint64_t size);
    
    Napi::Array LongArrayToNapiArray(const Napi::Env& env, long* arr, uint64_t size);
    void NapiArrayToLongArray(const Napi::Env& env, const Napi::Array& jsArr, long* arr, uint64_t size);
    
    Napi::Array FloatArrayToNapiArray(const Napi::Env& env, float* arr, uint64_t size);
    void NapiArrayToFloatArray(const Napi::Env& env, const Napi::Array& jsArr, float* arr, uint64_t size);
    
    Napi::Array DoubleArrayToNapiArray(const Napi::Env& env, double* arr, uint64_t size);
    void NapiArrayToDoubleArray(const Napi::Env& env, const Napi::Array& jsArr, double* arr, uint64_t size);

    // Object getters
    uint32_t getUint32OrDefault(const Napi::Object& obj, const std::string& key, uint32_t defaultValue);
    long getLongOrDefault(const Napi::Object& obj, const std::string& key, long defaultValue);
    float getFloatOrDefault(const Napi::Object& obj, const std::string& key, float defaultValue);
    std::string getStringOrDefault(const Napi::Object& obj, const std::string& key, const std::string& defaultValue);


    // Struct conversions
    Napi::Object CreateDataFormatObject(const Napi::Env& env, data_format_t* df);
    data_format_t* NapiObjectToDataFormatT(const Napi::Object& obj);

    Napi::Object CreateDataPositionsObject(const Napi::Env& env, data_positions_t* dp);
    data_positions_t* NapiObjectToDataPositionsT(const Napi::Object& obj);

    Napi::Object CreateDivisionObject(const Napi::Env& env, division_t* division);
    division_t* NapiObjectToDivisionT(const Napi::Object& obj);

    divisions_t* NapiObjectToDivisionsT(const Napi::Object& obj);

    Arguments* NapiObjectToArguments(const Napi::Object& obj);
    
} // namespace mscompress


#endif