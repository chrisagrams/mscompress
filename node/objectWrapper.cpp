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
    
    Napi::Array Uint64ArrayToNapiArray(const Napi::Env & env, uint64_t* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        for (uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }


    Napi::Array LongArrayToNapiArray(const Napi::Env& env, long* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        for(uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    Napi::Array FloatArrayToNapiArray(const Napi::Env& env, float* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        for(uint64_t i = 0; i < size; i++)
        {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    Napi::Array DoubleArrayToNapiArray(const Napi::Env& env, double* arr, uint64_t size) {
        Napi::Array jsArr = Napi::Array::New(env, size);
        for(uint64_t i = 0; i < size; i++) {
            jsArr.Set(i, Napi::Number::New(env, arr[i]));
        }
        return jsArr;
    }

    // Function to get an uint32 value from a Napi::Object with a default
    uint32_t getUint32OrDefault(const Napi::Object& obj, const std::string& key, uint32_t defaultValue) {
        if (obj.Has(key) && obj.Get(key).IsNumber()) {
            return obj.Get(key).As<Napi::Number>().Uint32Value();
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

        obj.Set("start_positions", Uint64ArrayToNapiArray(env, dp->start_positions, dp->total_spec));
        obj.Set("end_positions", Uint64ArrayToNapiArray(env, dp->end_positions, dp->total_spec));
        obj.Set("total_spec", Napi::Number::New(env, dp->total_spec));
        obj.Set("file_end", Napi::Number::New(env, dp->file_end)); //TODO: remove this

        return obj;
    }

    /* C division -> Napi */
    Napi::Object CreateDivisionObject(const Napi::Env& env, division_t* division) {
        Napi::Object obj = Napi::Object::New(env);

        obj.Set("spectra", CreateDataPositionsObject(env, division->spectra));
        obj.Set("xml", CreateDataPositionsObject(env, division->xml));
        obj.Set("mz", CreateDataPositionsObject(env, division->mz));
        obj.Set("inten", CreateDataPositionsObject(env, division->inten));
        obj.Set("size", Napi::Number::New(env, division->size));
        obj.Set("scans", LongArrayToNapiArray(env, division->scans, division->spectra->total_spec));
        obj.Set("ms_levels", LongArrayToNapiArray(env, division->ms_levels, division->spectra->total_spec));
        obj.Set("retention_times", FloatArrayToNapiArray(env, division->ret_times, division->spectra->total_spec));

        return obj;
    }
}