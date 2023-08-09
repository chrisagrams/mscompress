#include <napi.h>
#include "export.h"
#include "mscompress.h"

Napi::Number get_timeWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    double time = get_time();
    return Napi::Number::New(env, time);
}

Napi::Number get_num_threadsWrapped(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    int np = get_num_threads();
    return Napi::Number::New(env, np);
}

Napi::Object mscompress::Init(Napi::Env env, Napi::Object exports)
{
    exports.Set("getTime", Napi::Function::New(env, get_timeWrapped));
    exports.Set("getThreads", Napi::Function::New(env, get_num_threadsWrapped));
    return exports;
}