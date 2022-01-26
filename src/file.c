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

size_t
get_filesize(char* path)
{
    struct stat fi;

    stat(path, &fi);

    return fi.st_size;
}

ssize_t
write_to_file(int fd, char* buff, size_t n)
{
    if(fd < 0)
        exit(1);
    
    ssize_t rv;

    rv = write(fd, buff, n);

    if (rv < 0)
    {
        fprintf(stderr, "Error in writing %ld bytes to file descritor %d. Attempted to write %s", n, fd, buff);
        exit(1);
    }

    return rv;
}

ssize_t
read_from_file(int fd, void* buff, size_t n)
{
    if(fd < 0)
        exit(1);
    

    ssize_t rv;

    rv = read(fd, buff, n);

    if (rv < 0)
    {
        fprintf(stderr, "Error in read.\n");
        exit(1);
    }

    return rv;
    
}

off_t
get_offset(int fd)
{
    return lseek(fd, 0, SEEK_CUR);
}

void
write_header(int fd, char* xml_compression_method, char* binary_compression_method, long blocksize, char* md5)
/**
 * @brief Writes .msz header to file descriptor.
 * Header format:
 *              |====================================================|
 *              |        Content            |    Size    |  Offset   |
 *              |====================================================|
 *              | Magic Tag (0x035F51B5)    |   4  bytes |      0    |
 *              | Version Major Number      |   4  bytes |      4    |
 *              | Version Minor Number      |   4  bytes |      8    |
 *              | Message Tag               | 128  bytes |     12    |
 *              | XML Compression Method    |   4  bytes |    140    |
 *              | Binary Compression Method |   4  bytes |    144    |
 *              | Blocksize                 |   8  bytes |    148    |
 *              | MD5                       |  32  bytes |    156    |
 *              | Reserved                  | 324  bytes |    188    |
 *              |====================================================|
 *              | Total Size                |  512 bytes |           |
 *              |====================================================|
 */             
{
    char header_buff[HEADER_SIZE] = "";

    int* header_buff_cast = (int*)(&header_buff[0]);

    *header_buff_cast = MAGIC_TAG;

    *(header_buff_cast+1) = FORMAT_VERSION_MAJOR;

    *(header_buff_cast+2) = FORMAT_VERSION_MINOR;
    
    char message_buff[MESSAGE_SIZE] = MESSAGE;

    memcpy(header_buff+MESSAGE_OFFSET, message_buff, MESSAGE_SIZE);

    memcpy(header_buff+XML_COMPRESSION_METHOD_OFFSET, xml_compression_method, XML_COMPRESSION_METHOD_SIZE);

    memcpy(header_buff+BINARY_COMPRESSION_METHOD_OFFSET, binary_compression_method, BINARY_COMPRESSION_METHOD_SIZE);

    long* header_buff_cast_long = (long*)(&header_buff[0]+BLOCKSIZE_OFFSET);

    *header_buff_cast_long = blocksize;

    memcpy(header_buff+MD5_OFFSET, md5, MD5_SIZE);

    write_to_file(fd, header_buff, HEADER_SIZE);

}

long
get_header_blocksize(void* input_map)
{
    long* r;
    r = (long*)(&input_map[0]+BLOCKSIZE_OFFSET);
    return *r;
}

char*
get_header_xml_compresssion_type(void* input_map)
{
    char* buff;

    buff = (char*)malloc(XML_COMPRESSION_METHOD_SIZE);
    memcpy(buff, input_map+XML_COMPRESSION_METHOD_OFFSET, XML_COMPRESSION_METHOD_SIZE);

    return buff;
}

char*
get_header_binary_compression_type(void* input_map)
{
    char* buff;
    
    buff = (char*)malloc(BINARY_COMPRESSION_METHOD_SIZE);
    memcpy(buff, input_map+BINARY_COMPRESSION_METHOD_OFFSET, BINARY_COMPRESSION_METHOD_SIZE);

    return buff;
}


void
write_footer(footer_t footer, int fd)
{
    char buff[sizeof(footer_t)];
    footer_t* buff_cast = (footer_t*)(&buff[0]);
    *buff_cast = footer;
    write_to_file(fd, buff, sizeof(footer_t));
}

footer_t*
read_footer(void* input_map, long filesize)
{
    footer_t* footer;

    footer = (footer_t*)(&input_map[0]+filesize-sizeof(footer_t));

    return footer;
}


int
is_msz(int fd)
{
    char buff [512];
    char magic_buff[4];
    int* magic_buff_cast = (int*)(&magic_buff[0]);
    *magic_buff_cast = MAGIC_TAG;

    ssize_t rv;

    rv = read_from_file(fd, (void*)&buff, 512); /* Read 512 byte header from file*/ 

    lseek(fd, 0, SEEK_SET);
    
    if(rv != 512)
        return 0;

    if(strncmp(buff, magic_buff, 4) == 0)
        return 1;
    
    return 0;
    
}

int
is_mzml(int fd)
{
    char buff[512];
    ssize_t rv;

    rv = read_from_file(fd, (void*)&buff, 512);
    lseek(fd, 0, SEEK_SET);
    
    if(rv != 512)
        return 0;
    
    if(strstr(buff, "indexedmzML") != NULL)
        return 1;

    return 0;
        
}