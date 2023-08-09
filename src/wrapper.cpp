#include <napi.h>
#include "export.h"
#include "mscompress.h"

Napi::Number get_timeWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    double time = get_time();
    return Napi::Number::New(env, time);
}

Napi::Object mscompress::Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("getTime", Napi::Function::New(env, get_timeWrapped));
    return exports;
}