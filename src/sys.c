#ifdef __linux__
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif __APPLE__
#include <sys/sysctl.h>
#include <pthread.h>
#elif _WIN32
#include <sysinfoapi.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include "mscompress.h"


int
get_cpu_count()
/**
 * @brief A wrapper function to get the number of processor cores on a platform. 
 * Prints to stdout the number of processors detected.
 * 
 * @return Number of configured (available) processors.
 */
{
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
        /* TODO */
        SYSTEM_INFO sysinfo;

        GetSystemInfo(&sysinfo);

        np = sysinfo.dwNumberOfProcessors;
    #endif

    print("\t%d usable processors detected.\n", np);

    return np;
}

int
get_thread_id()
{
    uint64_t tid;

    #ifdef __linux__

        tid = (int)syscall(__NR_gettid);
    
    #elif __APPLE__

        pthread_threadid_np(NULL, &tid);

    #elif _WIN32
        
        /* TODO */
    
    #endif

    return (int)tid;
}

int
print(const char* format, ...)
/**
 * @brief printf() wrapper to print to console. Checks if program is running in verbose mode before printing.
 *        Drop-in replacement to printf().
 */
{
    if (verbose)
    {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);    
    }
}

int
error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(-1);
}

int
warning(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}