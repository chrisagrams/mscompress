#ifdef __linux__
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#elif __APPLE__
#include <pthread.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#elif _WIN32
#include <sysinfoapi.h>
#include <windows.h>
#endif

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mscompress.h"

int get_num_threads() {
   int np;

#ifdef __linux__

   np = (int)get_nprocs_conf();

#elif __APPLE__

   int mib[2];
   size_t len;

   mib[0] = CTL_HW;
   mib[1] = HW_NCPU;
   len = sizeof(np);

   sysctl(mib, 2, &np, &len, NULL, 0);

#elif _WIN32

   SYSTEM_INFO sysinfo;
   GetSystemInfo(&sysinfo);
   np = sysinfo.dwNumberOfProcessors;

#endif

   return np;
}

// void
// prepare_threads(long args_threads, long* n_threads)
// {
//     int np;

//     np = get_num_threads();

//     print("\t%d usable processors detected.\n", np);

//     if(args_threads == 0)
//       *n_threads = np;
//     else
//       *n_threads = args_threads;

//     print("\tUsing %d threads.\n", *n_threads);
// }

void prepare_threads(Arguments* args) {
   int np;

   np = get_num_threads();

   print("\t%d usable processors detected.\n", np);

   if (args->threads == 0)
      args->threads = np;

   print("\tUsing %d threads.\n", args->threads);
}

int get_thread_id() {
   uint64_t tid;

#ifdef __linux__

   tid = (int)syscall(__NR_gettid);

#elif __APPLE__

   pthread_threadid_np(NULL, &tid);

#elif _WIN32

   tid = (uint64_t)GetCurrentThreadId();

#endif

   return (int)tid;
}

double get_time() {
#ifdef _WIN32
   LARGE_INTEGER frequency, counter;
   QueryPerformanceFrequency(&frequency);
   QueryPerformanceCounter(&counter);
   return (double)counter.QuadPart / frequency.QuadPart;
#else
   struct timeval tv;
   gettimeofday(&tv, NULL);
   return tv.tv_sec + (tv.tv_usec / 1e6);
#endif
}

/* Global callback pointers for custom error/warning handling */
static error_callback_t custom_error_callback = NULL;
static warning_callback_t custom_warning_callback = NULL;

int print(const char* format, ...)
/**
 * @brief printf() wrapper to print to console. Checks if program is running in
 * verbose mode before printing. Drop-in replacement to printf().
 */
{
   int ret = -1;
   if (verbose) {
      va_list args;
      va_start(args, format);
      ret = vprintf(format, args);
      va_end(args);
   }
   return ret;
}

/**
 * @brief error() wrapper to print error messages to stderr. Always prints the
 * message regardless of the verbose flag. Drop-in replacement to fprintf(stderr, ...).
 * If a custom callback is set, it will be used instead of stderr.
 */
int error(const char* format, ...) {
   va_list args;
   va_start(args, format);
   
   if (custom_error_callback != NULL) {
      char buffer[4096];
      vsnprintf(buffer, sizeof(buffer), format, args);
      custom_error_callback(buffer);
   } else {
      vfprintf(stderr, format, args);
   }
   
   va_end(args);
   return 0;
}

int warning(const char* format, ...) {
   va_list args;
   va_start(args, format);
   
   if (custom_warning_callback != NULL) {
      char buffer[4096];
      vsnprintf(buffer, sizeof(buffer), format, args);
      custom_warning_callback(buffer);
   } else {
      vfprintf(stderr, format, args);
   }
   
   va_end(args);
   return 0;
}

void set_error_callback(error_callback_t callback) {
   custom_error_callback = callback;
}

void set_warning_callback(warning_callback_t callback) {
   custom_warning_callback = callback;
}

void reset_error_callback(void) {
   custom_error_callback = NULL;
}

void reset_warning_callback(void) {
   custom_warning_callback = NULL;
}

long parse_blocksize(char* arg) {
   int num;
   int len;
   char prefix[2];
   long res = -1;

   len = strlen(arg);
   num = atoi(arg);

   memcpy(prefix, arg + len - 2, 2);

   if (!strcmp(prefix, "KB") || !strcmp(prefix, "kb"))
      res = num * 1e+3;
   else if (!strcmp(prefix, "MB") || !strcmp(prefix, "mb"))
      res = num * 1e+6;
   else if (!strcmp(prefix, "GB") || !strcmp(prefix, "gb"))
      res = num * 1e+9;

   return res;
}