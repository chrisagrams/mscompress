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
#include <stdio.h>
#include <string.h>


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
write_header(int fd, char* compression_method, char* md5)
/**
 * @brief Writes .msz header to file descriptor.
 * Header format:
 *              |================================================|
 *              |        Content        |    Size    |  Offset   |
 *              |================================================|
 *              | Magic Tag (0x035F51B5)|   4  bytes |      0    |
 *              | Version Major Number  |   4  bytes |      4    |
 *              | Version Minor Number  |   4  bytes |      8    |
 *              | Message Tag           | 128  bytes |     12    |
 *              | Compression Method    |   4  bytes |    140    |
 *              | MD5                   |  32  bytes |    144    |
 *              | Reserved              | 336  bytes |    176    |
 *              |================================================|
 *              | Total Size            |  512 bytes |           |
 *              |================================================|
 */             
{
    char header_buff[512] = "";
    // memset(header_buff, NULL, 512);

    int* header_buff_cast = (int*)(&header_buff[0]);

    *header_buff_cast = MAGIC_TAG;

    *(header_buff_cast+1) = FORMAT_VERSION_MAJOR;

    *(header_buff_cast+2) = FORMAT_VERSION_MINOR;
    
    char message_buff[128] = MESSAGE;

    memcpy(header_buff+12, message_buff, 128);

    memcpy(header_buff+140, compression_method, 4);

    memcpy(header_buff+144, md5, 32);

    write_to_file(fd, header_buff, 512);

}