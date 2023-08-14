#include <napi.h>
using namespace std;

namespace mscompress
{
    //Export API
    Napi::Object Init(Napi::Env env, Napi::Object exports);
    NODE_API_MODULE(mscompress, Init)
}
