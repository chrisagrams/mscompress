#include "../include/progress.h"

namespace mscompress {

/**
 * @brief C callback bridge for progress reporting from worker threads
 * This function is called by C worker threads and safely marshals
 * progress updates to the JavaScript event loop using ThreadSafeFunction.
 */
extern "C" void progress_callback_bridge(int thread_id, int completed, int total, void* user_data) {
    if (user_data == nullptr) {
        return;
    }

    ProgressData* data = static_cast<ProgressData*>(user_data);
    
    // Create a new ProgressUpdate object for this callback
    ProgressUpdate* update = new ProgressUpdate{thread_id, completed, total};
    
    // Call the thread-safe function (non-blocking)
    // The lambda will be executed on the JavaScript thread
    napi_status status = data->tsfn.NonBlockingCall(update, 
        [](Napi::Env env, Napi::Function jsCallback, ProgressUpdate* update) {
            if (jsCallback != nullptr) {
                jsCallback.Call({
                    Napi::Number::New(env, update->thread_id),
                    Napi::Number::New(env, update->completed),
                    Napi::Number::New(env, update->total)
                });
            }
            delete update;
        }
    );

    // If the call failed, clean up the update
    if (status != napi_ok) {
        delete update;
    }
}

}
