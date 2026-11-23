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

/**
 * @brief Returns the number of available CPU threads on the current system.
 * @return The number of available processors/threads.
 */
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

//     MSZ_INFO("\t%d usable processors detected.\n", np);

//     if(args_threads == 0)
//       *n_threads = np;
//     else
//       *n_threads = args_threads;

//     MSZ_INFO("\tUsing %d threads.\n", *n_threads);
// }

/**
 * @brief Detects available CPUs and sets the thread count in args.
 * @param args A pointer to the Arguments struct. If `args->threads` is 0, it is
 *             set to the number of available processors.
 */
void prepare_threads(Arguments* args) {
   int np;

   np = get_num_threads();

   MSZ_INFO("\t%d usable processors detected.\n", np);

   if (args->threads == 0)
      args->threads = np;

   MSZ_INFO("\tUsing %d threads.\n", args->threads);
}

/**
 * @brief Returns the OS-level thread ID of the calling thread.
 * @return The thread ID as an integer.
 */
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

/**
 * @brief Returns the current wall-clock time in seconds with microsecond precision.
 * @return The current time as a double (seconds since epoch).
 */
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

/* Logging state. Set once at startup (CLI) or module import (bindings) and
   only read afterwards, so worker threads log without locking. */
static msz_log_handler_t log_handler = msz_default_log_handler;
static void* log_handler_user = NULL;
static volatile int log_level = MSZ_LOG_WARN;

/**
 * @brief Default log handler. INFO/DEBUG go to stdout, WARN/ERROR to stderr,
 *        written verbatim (messages carry their own newlines).
 * @param rec The log record to write.
 * @param user Unused.
 */
void msz_default_log_handler(const msz_log_record_t* rec, void* user)
{
   (void)user;
   fputs(rec->message, rec->level >= MSZ_LOG_WARN ? stderr : stdout);
}

/**
 * @brief Registers the handler that receives every log record at or above the
 *        current log level.
 * @param handler The handler to invoke, or `NULL` to restore the default.
 * @param user Opaque pointer passed back to the handler on every call.
 */
void msz_set_log_handler(msz_log_handler_t handler, void* user)
{
   log_handler = handler != NULL ? handler : msz_default_log_handler;
   log_handler_user = handler != NULL ? user : NULL;
}

/**
 * @brief Sets the minimum level that reaches the log handler. Defaults to
 *        `MSZ_LOG_WARN`; the CLI's `-v` lowers it to `MSZ_LOG_INFO`.
 * @param level The minimum level to emit.
 */
void msz_set_log_level(msz_log_level_t level) { log_level = level; }

/**
 * @brief Returns the current minimum log level.
 * @return The minimum level that reaches the log handler.
 */
msz_log_level_t msz_get_log_level(void) { return (msz_log_level_t)log_level; }

/**
 * @brief Formats a message and passes it to the log handler. Use through the
 *        `MSZ_ERROR`/`MSZ_WARN`/`MSZ_INFO`/`MSZ_DEBUG` macros, which fill in
 *        the call site.
 * @param level Severity of the record.
 * @param file Source file of the call site.
 * @param line Source line of the call site.
 * @param func Function name of the call site.
 * @param format The printf-style format string.
 * @param ... Variable arguments matching the format string.
 */
void msz_log(msz_log_level_t level, const char* file, int line,
             const char* func, const char* format, ...)
{
   if ((int)level < log_level)
      return;

   char buffer[4096];
   va_list args;
   va_start(args, format);
   vsnprintf(buffer, sizeof(buffer), format, args);
   va_end(args);

   msz_log_record_t rec = {level, file, line, func, buffer};
   log_handler(&rec, log_handler_user);
}

/**
 * @brief Parses a human-readable block size string (e.g., "100MB") into bytes.
 * @param arg The block size string with a KB, MB, or GB suffix.
 * @return The block size in bytes, or -1 if the suffix is unrecognized.
 */
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