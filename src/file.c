/**
 * @file file.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to provide platform-dependent low level file read/write operations.
 * @version 0.0.1
 * @date 2021-12-21
 * 
 * @copyright 
 * 
 */

#include "mscompress.h"
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>


void* 
get_mapping(int fd)
/**
 * @brief A memory mapping wrapper. Implements mmap() syscall on POSIX systems and MapViewOfFile on Windows platform.
 * 
 */
{
    int status;
    
    struct stat buff;

    if (fd == -1) return NULL;

    status = fstat(fd, &buff);

    if (status == -1 ) return NULL;

    return mmap(NULL, buff.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
}

int 
remove_mapping(void* addr, int fd)
{
    return munmap(addr, fd);
}

int
get_blksize(char* path)
{
    struct stat fi;

    stat(path, &fi);

    return fi.st_blksize;
}

int
write_to_file(int fd, char* buff, size_t n)
{
    if(fd < 0)
        exit(1);
    
    int rv;

    rv = write(fd, buff, n);

    if (rv < 0)
    {
        fprintf(stderr, "Error in writing %ld bytes to file descritor %d. Attempted to write %s", n, fd, buff);
        exit(1);
    }
}

void
write_header(int fd)
{
    char* magic_tag[4];
    *magic_tag = MAGIC_TAG;
    write_to_file(fd, magic_tag, 4);

    char header_text[128] = MESSAGE;

    write_to_file(fd, header_text, 128);

}