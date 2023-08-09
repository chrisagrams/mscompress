#include <napi.h>
using namespace std;

namespace mscompress
{
    //define example function
    int add(int x, int y);

    //add function wrapper
    Napi::Number addWrapped(const Napi::CallbackInfo& info);

    //Export API
    Napi::Object Init(Napi::Env env, Napi::Object exports);
    NODE_API_MODULE(mscompress, Init)
}
