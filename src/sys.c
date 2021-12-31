#ifdef __linux__
    #include <sys/sysinfo.h>
    #include <sys/types.h>
    #include <unistd.h>
    #include <sys/syscall.h>
#elif __APPLE__
    #include <sys/sysctl.h>
#elif _WIN32
    #include <sysinfoapi.h>
#endif
#include "mscompress.h"
#include <stdlib.h>
#include <stdio.h>

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

    printf("\t%d usable processors detected.\n", np);

    return np;
}

int
get_thread_id()
{
    int tid;

    #ifdef __linux__

        tid = (int)syscall(__NR_gettid);
    
    #elif __APPLE__

        tid = (int)pthread_getthreadid_np();

    #elif _WIN32
        
        /* TODO */
    
    #endif

    return tid;
}