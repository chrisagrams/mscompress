#include <napi.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include "export.h"

/* Helpers */

namespace mscompress { 

    static std::unordered_map<std::string, uint32_t> string_to_accession_map = {
        {"_32i_", 1000519},
        {"_16e_", 1000520},
        {"_32f_", 1000521},
        {"_64i_", 1000522},
        {"_64d_", 1000523},
        {"_zlib_", 1000574},
        {"_no_comp_", 1000576},
        {"_intensity_", 1000515},
        {"_mass_", 1000514},
        {"_xml_", 1000513},
        {"_lossless_", 4700000},
        {"_ZSTD_compression_", 4700001},
        {"_LZ4_compression_",  4700012},
        {"_cast_64_to_32_", 4700002},
        {"_log2_transform_", 4700003},
        {"_delta16_transform_", 4700004},
        {"_delta24_transform_", 4700005},
        {"_delta32_transform_", 4700006},
        {"_vbr_", 4700007},
        {"_bitpack_", 4700008},
        {"_vdelta16_transform_", 4700009},
        {"_vdelta24_transform_", 4700010},
        {"_cast_64_to_16_", 4700011},
    };

    // Accession to string function
    std::string AccessionToString(uint32_t accession) {
        for (const auto& pair : string_to_accession_map) {
            if (pair.second == accession) {
                return pair.first;
            }
        }
        return std::to_string(accession); // return the accession as string if not found
    }

    // String to accession function
    uint32_t StringToAccession(const std::string& str) {
        auto it = string_to_accession_map.find(str);
        if (it != string_to_accession_map.end()) {
            return it->second;
        }
        return 0;  // or whatever default value you'd like
    }
    
    /* C uint64 -> Napi */
    Napi::Array Uint64ArrayToNapiArray(const Napi::Env & env, uint64_t* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        if(arr == nullptr) return jsArr; // return empty array if arr is null
        for (uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    /* Napi -> C uint64 */
    void NapiArrayToUint64Array(const Napi::Env& env, const Napi::Array& jsArr, uint64_t* arr, uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            Napi::Value val = jsArr.Get(i);
            if (val.IsNumber()) {
                arr[i] = val.As<Napi::Number>().Int64Value();
            } else {
                Napi::TypeError::New(env, "Array element is not a number").ThrowAsJavaScriptException();
                return;
            }
        }
    }

    /* C uint32 -> Napi */
    Napi::Array Uint32ArrayToNapiArray(const Napi::Env & env, uint32_t* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        if(arr == nullptr) return jsArr; // return empty array if arr is null
        for (uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    /* Napi -> C uint32 */
    void NapiArrayToUint32Array(const Napi::Env& env, const Napi::Array& jsArr, uint32_t* arr, uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            Napi::Value val = jsArr.Get(i);
            if (val.IsNumber()) {
                arr[i] = val.As<Napi::Number>().Int64Value();
            } else {
                Napi::TypeError::New(env, "Array element is not a number").ThrowAsJavaScriptException();
                return;
            }
        }
    }

    /* C uint16 -> Napi */
    Napi::Array Uint16ArrayToNapiArray(const Napi::Env & env, uint16_t* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        if(arr == nullptr) return jsArr; // return empty array if arr is null
        for (uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    /* Napi -> C uint16 */
    void NapiArrayToUint16Array(const Napi::Env& env, const Napi::Array& jsArr, uint16_t* arr, uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            Napi::Value val = jsArr.Get(i);
            if (val.IsNumber()) {
                arr[i] = val.As<Napi::Number>().Int64Value();
            } else {
                Napi::TypeError::New(env, "Array element is not a number").ThrowAsJavaScriptException();
                return;
            }
        }
    }

    /* C long -> Napi */
    Napi::Array LongArrayToNapiArray(const Napi::Env& env, long* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        if(arr == nullptr) return jsArr; // return empty array if arr is null
        for(uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    /* Napi -> C long */
    void NapiArrayToLongArray(const Napi::Env& env, const Napi::Array& jsArr, long* arr, uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            Napi::Value val = jsArr.Get(i);
            if (val.IsNumber()) {
                arr[i] = val.As<Napi::Number>().Int64Value();
            } else {
                Napi::TypeError::New(env, "Array element is not a number").ThrowAsJavaScriptException();
                return;
            }
        }
    }

    /* C float -> Napi */
    Napi::Array FloatArrayToNapiArray(const Napi::Env& env, float* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        if(arr == nullptr) return jsArr; // return empty array if arr is null
        for(uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    /* Napi -> C float */
    void NapiArrayToFloatArray(const Napi::Env& env, const Napi::Array& jsArr, float* arr, uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            Napi::Value val = jsArr.Get(i);
            if (val.IsNumber()) {
                arr[i] = val.As<Napi::Number>().FloatValue();
            } else {
                Napi::TypeError::New(env, "Array element is not a number").ThrowAsJavaScriptException();
                return;
            }
        }
    }
    

    /* C double -> Napi */
    Napi::Array DoubleArrayToNapiArray(const Napi::Env& env, double* arr, uint64_t size) {
        if (arr == nullptr) return Napi::Array::New(env, 0); // return empty array if arr is null
        Napi::Array jsArr = Napi::Array::New(env, size);
        for(uint64_t i = 0; i < size; i++) {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    /* Napi -> C double */
    void NapiArrayToDoubleArray(const Napi::Env& env, const Napi::Array& jsArr, double* arr, uint64_t size) {
        for (uint64_t i = 0; i < size; i++) {
            Napi::Value val = jsArr.Get(i);
            if (val.IsNumber()) {
                arr[i] = val.As<Napi::Number>().DoubleValue();
            } else {
                Napi::TypeError::New(env, "Array element is not a number").ThrowAsJavaScriptException();
                return;
            }
        }
    }

    // Function to get an uint32 value from a Napi::Object with a default
    uint32_t getUint32OrDefault(const Napi::Object& obj, const std::string& key, uint32_t defaultValue) {
        if (obj.Has(key) && obj.Get(key).IsNumber()) {
            return obj.Get(key).As<Napi::Number>().Uint32Value();
        }
        return defaultValue;
    }

    long getLongOrDefault(const Napi::Object& obj, const std::string& key, long defaultValue) {
        if (obj.Has(key) && obj.Get(key).IsNumber()) {
           return obj.Get(key).As<Napi::Number>().Int64Value();
        }
        return defaultValue;
    }

    // Function to get a float value from a Napi::Object with a default
    float getFloatOrDefault(const Napi::Object& obj, const std::string& key, float defaultValue) {
        if (obj.Has(key) && obj.Get(key).IsNumber()) {
            return obj.Get(key).As<Napi::Number>().FloatValue();
        }
        return defaultValue;
    }

    // Function to get a string value from a Napi::Object with a default
    std::string getStringOrDefault(const Napi::Object& obj, const std::string& key, const std::string& defaultValue) {
        if (obj.Has(key) && obj.Get(key).IsString()) {
            return obj.Get(key).As<Napi::String>().Utf8Value();
        }
        return defaultValue;
    }

    /* C df -> Napi */
    Napi::Object CreateDataFormatObject(const Napi::Env& env, data_format_t* df) {
        Napi::Object obj = Napi::Object::New(env);

        obj.Set("source_mz_fmt", Napi::String::New(env, AccessionToString(df->source_mz_fmt)));
        obj.Set("source_inten_fmt", Napi::String::New(env, AccessionToString(df->source_inten_fmt)));
        obj.Set("source_compression", Napi::String::New(env, AccessionToString(df->source_compression)));
        obj.Set("source_total_spec", Napi::Number::New(env, df->source_total_spec));

        return obj;
    }

    /* Napi -> C df */
    data_format_t* NapiObjectToDataFormatT(const Napi::Object& obj) {
        data_format_t* df = new data_format_t();

        std::string source_mz_fmt_str = getStringOrDefault(obj, "source_mz_fmt", "");
        df->source_mz_fmt = StringToAccession(source_mz_fmt_str);

        std::string source_inten_fmt_str = getStringOrDefault(obj, "source_inten_fmt", "");
        df->source_inten_fmt = StringToAccession(source_inten_fmt_str);

        std::string source_compression_str = getStringOrDefault(obj, "source_compression", "");
        df->source_compression = StringToAccession(source_compression_str);

        df->source_total_spec = getUint32OrDefault(obj, "source_total_spec", 0);

        std::string target_xml_format_str = getStringOrDefault(obj, "target_xml_format", "");
        df->target_xml_format = StringToAccession(target_xml_format_str);

        std::string target_mz_format_str = getStringOrDefault(obj, "target_mz_format", "");
        df->target_mz_format = StringToAccession(target_mz_format_str);

        std::string target_inten_format_str = getStringOrDefault(obj, "target_inten_format", "");
        df->target_inten_format = StringToAccession(target_inten_format_str);

        df->mz_scale_factor = getFloatOrDefault(obj, "mz_scale_factor", 1.0f);
        df->int_scale_factor = getFloatOrDefault(obj, "int_scale_factor", 1.0f);

        return df;
    }

    /* C dp -> Napi */
    Napi::Object CreateDataPositionsObject(const Napi::Env& env, data_positions_t* dp) {
        Napi::Object obj = Napi::Object::New(env);

        if (dp == nullptr) return obj; // return empty object if dp is null

        obj.Set("start_positions", Uint64ArrayToNapiArray(env, dp->start_positions, dp->total_spec));
        obj.Set("end_positions", Uint64ArrayToNapiArray(env, dp->end_positions, dp->total_spec));
        obj.Set("total_spec", Napi::Number::New(env, dp->total_spec));
        obj.Set("file_end", Napi::Number::New(env, dp->file_end)); //TODO: remove this

        return obj;
    }

    /* Napi -> C dp */
    data_positions_t* NapiObjectToDataPositionsT(const Napi::Object& obj) {
        data_positions_t* dp = new data_positions_t();

        dp->total_spec = getUint32OrDefault(obj, "total_spec", 0);
        dp->file_end = getUint32OrDefault(obj, "file_end", 0);

        dp->start_positions = new uint64_t[dp->total_spec];
        dp->end_positions = new uint64_t[dp->total_spec];

        NapiArrayToUint64Array(obj.Env(), obj.Get("start_positions").As<Napi::Array>(), dp->start_positions, dp->total_spec);
        NapiArrayToUint64Array(obj.Env(), obj.Get("end_positions").As<Napi::Array>(), dp->end_positions, dp->total_spec);

        return dp;
    }

    /* C division -> Napi */
    Napi::Object CreateDivisionObject(const Napi::Env& env, division_t* division) {
        Napi::Object obj = Napi::Object::New(env);

        obj.Set("spectra", CreateDataPositionsObject(env, division->spectra));
        obj.Set("xml", CreateDataPositionsObject(env, division->xml));
        obj.Set("mz", CreateDataPositionsObject(env, division->mz));
        obj.Set("inten", CreateDataPositionsObject(env, division->inten));
        obj.Set("size", Napi::Number::New(env, division->size));
        obj.Set("scans", Uint32ArrayToNapiArray(env, division->scans, division->mz->total_spec));
        obj.Set("ms_levels", Uint16ArrayToNapiArray(env, division->ms_levels, division->mz->total_spec));
        obj.Set("retention_times", FloatArrayToNapiArray(env, division->ret_times, division->mz->total_spec));

        return obj;
    }

    /* Napi -> C division */
    division_t* NapiObjectToDivisionT(const Napi::Object& obj) {
        division_t* division = new division_t();

        division->spectra = NapiObjectToDataPositionsT(obj.Get("spectra").As<Napi::Object>());
        division->xml = NapiObjectToDataPositionsT(obj.Get("xml").As<Napi::Object>());
        division->mz = NapiObjectToDataPositionsT(obj.Get("mz").As<Napi::Object>());
        division->inten = NapiObjectToDataPositionsT(obj.Get("inten").As<Napi::Object>());
        division->size = getUint32OrDefault(obj, "size", 0);
        division->scans = new uint32_t[division->spectra->total_spec];
        division->ms_levels = new uint16_t[division->spectra->total_spec];
        division->ret_times = new float[division->spectra->total_spec];

        NapiArrayToUint32Array(obj.Env(), obj.Get("scans").As<Napi::Array>(), division->scans, division->spectra->total_spec);
        NapiArrayToUint16Array(obj.Env(), obj.Get("ms_levels").As<Napi::Array>(), division->ms_levels, division->spectra->total_spec);
        NapiArrayToFloatArray(obj.Env(), obj.Get("retention_times").As<Napi::Array>(), division->ret_times, division->spectra->total_spec);

        return division;
    }

    /* Napi -> C divisions (multiple) */
    divisions_t* NapiObjectToDivisionsT(const Napi::Object& obj) {
        divisions_t* divisions = new divisions_t();
        Napi::Array divisionsArray = obj.Get("divisions").As<Napi::Array>();
        divisions->divisions = new division_t*[divisionsArray.Length()];
        divisions->n_divisions = divisionsArray.Length();

        for (uint32_t i = 0; i < divisionsArray.Length(); i++) {
            Napi::Object division = divisionsArray.Get(i).As<Napi::Object>();
            divisions->divisions[i] = NapiObjectToDivisionT(division);
        }
        return divisions;
    }

    /* Napi -> C arguments */
    Arguments* NapiObjectToArguments(const Napi::Object& obj) {
        Arguments* args = new Arguments();

        // Init arguments
        init_args(args);

        // Set arguments
        set_threads(args, getUint32OrDefault(obj, "threads", 1));
        
        args->target_xml_format = StringToAccession(getStringOrDefault(obj, "target_xml_format", "_ZSTD_compression_"));
        args->target_mz_format = StringToAccession(getStringOrDefault(obj, "target_mz_format", "_ZSTD_compression_"));
        args->target_inten_format = StringToAccession(getStringOrDefault(obj, "target_inten_format", "_ZSTD_compression_"));

        args->zstd_compression_level = getUint32OrDefault(obj, "zstd_compression_level", 3);

        args->ms_level = getLongOrDefault(obj, "ms_level", 0);


        Napi::Value scans = obj.Get("scans");
            if (scans.IsArray()) {
                Napi::Array scansArray = scans.As<Napi::Array>();
                args->scans_length = scansArray.Length();
                args->scans = new uint32_t[args->indices_length];
                for (size_t i = 0; i < args->scans_length; ++i) {
                    args->scans[i] = scansArray.Get(i).ToNumber().Int64Value();
                }
            } else {
                args->scans_length = 0;
                args->scans = nullptr;
        }
        
        Napi::Value indiciesValue = obj.Get("indices");
        if (indiciesValue.IsArray()) {
            Napi::Array indiciesArray = indiciesValue.As<Napi::Array>();
            args->indices_length = indiciesArray.Length();
            args->indices = new long[args->indices_length];
            for (size_t i = 0; i < args->indices_length; ++i) {
                args->indices[i] = indiciesArray.Get(i).ToNumber().Int64Value();
            }
        } else {
            args->indices_length = 0;
            args->indices = nullptr;
        }

        return args;
    }
}