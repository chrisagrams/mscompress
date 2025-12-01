#ifndef MSCOMPRESS_PROGRESS_H
#define MSCOMPRESS_PROGRESS_H

#include <napi.h>
#include <atomic>
#include <mutex>

namespace mscompress {

/**
 * @brief Progress data structure for thread-safe progress reporting
 */
struct ProgressData {
    Napi::ThreadSafeFunction tsfn;
    std::atomic<int> completed_spectra{0};
    std::atomic<int> total_spectra{0};
    std::mutex mutex;
};

/**
 * @brief Progress update structure passed to JavaScript callback
 */
struct ProgressUpdate {
    int thread_id;
    int completed;
    int total;
};

/**
 * @brief C callback bridge for progress reporting from worker threads
 * This function is called by C worker threads and safely marshals
 * progress updates to the JavaScript event loop.
 * 
 * @param thread_id The ID of the worker thread reporting progress
 * @param completed Number of spectra completed by this thread
 * @param total Total number of spectra this thread will process
 * @param user_data Pointer to ProgressData structure
 */
extern "C" void progress_callback_bridge(int thread_id, int completed, int total, void* user_data);

}

#endif // MSCOMPRESS_PROGRESS_H
