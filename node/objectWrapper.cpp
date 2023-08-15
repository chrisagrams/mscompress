#include <napi.h>
#include <iostream>
#include "export.h"

/* Helpers */

namespace mscompress { 
    
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

    /* C df -> Napi */
    Napi::Object CreateDataFormatObject(const Napi::Env& env, data_format_t* df) {
        Napi::Object obj = Napi::Object::New(env);

        obj.Set("source_mz_fmt", Napi::Number::New(env, df->source_mz_fmt));
        obj.Set("source_inten_fmt", Napi::Number::New(env, df->source_inten_fmt));
        obj.Set("source_compression", Napi::Number::New(env, df->source_compression));
        obj.Set("source_total_spec", Napi::Number::New(env, df->source_total_spec));

        return obj;
    }

    /* Napi -> C df */
    data_format_t* NapiObjectToDataFormatT(const Napi::Object& obj) {
        data_format_t* df = new data_format_t();


        df->source_mz_fmt = getUint32OrDefault(obj, "source_mz_fmt", 0);
        df->source_inten_fmt = getUint32OrDefault(obj, "source_inten_fmt", 0);
        df->source_compression = getUint32OrDefault(obj, "source_compression", 0);
        df->source_total_spec = getUint32OrDefault(obj, "source_total_spec", 0);

        df->target_xml_format = getUint32OrDefault(obj, "target_xml_format", 0);
        df->target_mz_format = getUint32OrDefault(obj, "target_mz_format", 0);
        df->target_inten_format = getUint32OrDefault(obj, "target_inten_format", 0);

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

        return obj;
    }
}