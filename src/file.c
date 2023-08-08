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

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "mscompress.h"

#ifdef _WIN32
    #include <Windows.h>
    #include <io.h>
    #include <fcntl.h>
    #define open _open
    #define close _close
    #define read _read
    #define write _write
    #define lseek64 _lseeki64
    #define ssize_t int
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif


void* 
get_mapping(int fd)
/**
 * @brief A memory mapping wrapper. Implements mmap() syscall on POSIX systems and MapViewOfFile on Windows platform.
 * 
 */
{
    int status;
    
    struct stat buff;

    void* mapped_data = NULL;

    if (fd == -1) return NULL;

    status = fstat(fd, &buff);

    if (status == -1 ) return NULL;

    #ifdef _WIN32

        HANDLE hFile = (HANDLE)_get_osfhandle(fd);
        HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);

        if (hMapping != NULL)
        {
            mapped_data = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, buff.st_size);
            CloseHandle(hMapping);
        }

    #else

        mapped_data = mmap(NULL, buff.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    #endif

    return mapped_data;
}

int 
remove_mapping(void* addr, int fd)
{
    int result = -1;

    #ifdef _WIN32

        result = UnmapViewOfFile(addr);

    #else

        result = munmap(addr, fd);

    #endif

    return result;
}

size_t
get_filesize(char* path)
{
    struct stat fi;

    stat(path, &fi);

    return fi.st_size;
}

long
update_fd_pos(int fd, long increment)
{
  for(int i = 0; i < 3; i++)
  {
    if(fds[i] == fd)
    {
      fd_pos[i] += increment;
      return fd_pos[i];
    }
  }
  return 0;
}

size_t 
write_to_file(int fd, char* buff, size_t n)
{
    if (fd < 0)
        error("write_to_file: invalid file descriptor.\n");

    ssize_t rv;

    #ifdef _WIN32
        rv = write(fd, buff, (unsigned int)n);
    #else
        rv = write(fd, buff, n);
    #endif

    if (rv < 0)
        error("Error in writing %ld bytes to file descriptor %d. Attempted to write %s", n, fd, buff);

    //Debug
    long pos = get_offset(fd);
    print("write_to_file: curr offset: %ld\n", pos);

    if(!update_fd_pos(fd, rv))
      error("write_to_file: error in updating fd pos\n");

    return (size_t)rv;
}

size_t 
read_from_file(int fd, void* buff, size_t n)
{
    if (fd < 0)
        error("read_from_file: invalid file descriptor.\n");

    ssize_t rv;

    #ifdef _WIN32
        rv = read(fd, buff, (unsigned int)n);
    #else
        rv = read(fd, buff, n);
    #endif

    if (rv < 0)
        error("Error in reading %ld bytes from file descriptor %d.", n, fd);

    return (size_t)rv;
}

long
get_offset(int fd)
{
  // long ret = (long)lseek64(fd, 0, SEEK_CUR);
  // if (ret == -1)
  // {
  //   perror("lseek64");
  //   exit(-1);
  // }
  // return ret;
  for(int i = 0; i < 3; i++)
  {
    if(fds[i] == fd)
      return fd_pos[i];
  }
  error("get_offset: invalid fd\n");
  exit(-1);
}

char* serialize_df(data_format_t* df)
{
  char* r = calloc(1, DATA_FORMAT_T_SIZE);
  if(r == NULL)
    error("serialize_df: malloc failed.\n");
  
  size_t offset = 0;

  /* source information (source mzML) */
  memcpy(r + offset, &df->source_mz_fmt, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(r + offset, &df->source_inten_fmt, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(r + offset, &df->source_compression, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(r + offset, &df->source_total_spec, sizeof(uint32_t));
  offset += sizeof(uint32_t);

  /* target information (target msz) */
  memcpy(r + offset, &df->target_xml_format, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(r + offset, &df->target_mz_format, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(r + offset, &df->target_inten_format, sizeof(uint32_t));
  offset += sizeof(uint32_t);

  /* algo parameters */
  memcpy(r + offset, &df->mz_scale_factor, sizeof(float));
  offset += sizeof(float);
  memcpy(r + offset, &df->int_scale_factor, sizeof(float));
  offset += sizeof(float);

  return r;
}

data_format_t* deserialize_df(char* buff)
{
  data_format_t* r = malloc(sizeof(data_format_t));
  if(r == NULL)
    error("deserialize_df: malloc failed.\n");

  size_t offset = 0;

  /* source information (source mzML) */
  memcpy(&r->source_mz_fmt, buff + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(&r->source_inten_fmt, buff + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(&r->source_compression, buff + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(&r->source_total_spec, buff + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);

  /* target information (target msz) */
  memcpy(&r->target_xml_format, buff + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(&r->target_mz_format, buff + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);
  memcpy(&r->target_inten_format, buff + offset, sizeof(uint32_t));
  offset += sizeof(uint32_t);

  /* algo parameters */
  memcpy(&r->mz_scale_factor, buff + offset, sizeof(float));
  offset += sizeof(float);
  memcpy(&r->int_scale_factor, buff + offset, sizeof(float));
  offset += sizeof(float);

  return r;
}

void
write_header(int fd, data_format_t* df, long blocksize, char* md5)
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
 *              | Source m/z format         |   4  bytes |    140    |
 *              | Source intensity format   |   4  bytes |    144    |
 *              | Source compression format |   4  bytes |    148    |
 *              | Source spectrum count     |   4  bytes |    152    |
 *              | Target XML format         |   4  bytes |    156    |
 *              | Target m/z format         |   4  bytes |    160    |
 *              | Target intensity format   |   4  bytes |    164    |
 *              | mz scale factor           |   4  bytes |    168    |
 *              | int scale factor          |   4  bytes |    172    |
 *              | Blocksize                 |   8  bytes |    176    |
 *              | MD5                       |  32  bytes |    184    |
 *              | Reserved                  |  296 bytes |    216    |
 *              |====================================================|
 *              | Total Size                |  512 bytes |           |
 *              |====================================================|
 */             
{
    // Allocate header_buff
    char header_buff[HEADER_SIZE] = "";

    // Interpret #defines 
    char message_buff[MESSAGE_SIZE] = MESSAGE;
    int magic_tag = MAGIC_TAG;
    int format_version_major = FORMAT_VERSION_MAJOR;
    int format_version_minor = FORMAT_VERSION_MINOR;

    memcpy(header_buff, &magic_tag, sizeof(magic_tag));
    memcpy(header_buff + sizeof(magic_tag), &format_version_major, sizeof(format_version_major));
    memcpy(header_buff + sizeof(magic_tag) + sizeof(format_version_major), &format_version_minor, sizeof(format_version_minor));

    memcpy(header_buff + MESSAGE_OFFSET, message_buff, MESSAGE_SIZE);

    char* df_buff = serialize_df(df);
    memcpy(header_buff + DATA_FORMAT_T_OFFSET, df_buff, DATA_FORMAT_T_SIZE);

    memcpy(header_buff + BLOCKSIZE_OFFSET, &blocksize, sizeof(blocksize));

    memcpy(header_buff + MD5_OFFSET, md5, MD5_SIZE);

    write_to_file(fd, header_buff, HEADER_SIZE);


}

long
get_header_blocksize(void* input_map)
/**
 * @brief Gets blocksize stored in header.
 * 
 * @param input_map mmap'ed input file.
 * 
 * @return long value representing blocksize.
 */
{
    long* r;
    r = (long*)((uint8_t*)input_map + BLOCKSIZE_OFFSET);
    return *r;
}

data_format_t*
get_header_df(void* input_map)
{
  data_format_t* r;
  
  r = deserialize_df((char*)((uint8_t*)input_map + DATA_FORMAT_T_OFFSET));

  r->populated = 2;

  return r;
}

void
write_footer(footer_t* footer, int fd)
/**
 * @brief Writes a footer_t struct to file descritor.
 * 
 * @param footer A populated footer_t struct.
 * 
 * @param fd Output file descriptor to write to. 
 */
{
    footer->magic_tag = MAGIC_TAG; // Set magic tag
    write_to_file(fd, (char*)footer, sizeof(footer_t));
}

footer_t*
read_footer(void* input_map, long filesize)
/**
 * @brief Maps footer section of mmap'ed input file to a footer_t* pointer. 
 * 
 * @param input_map mmap'ed input file.
 * 
 * @param filesize Size of input filesize. 
 * 
 * @return Pointer to a footer_t struct located at the footer section of mmap'ed file.
 */
{
    footer_t* footer;

    footer = (footer_t*)((char*)input_map + filesize - sizeof(footer_t));

    if (footer->magic_tag != MAGIC_TAG)
        error("read_footer: invalid magic tag.\n");

    return footer;
}

int
is_msz(int fd)
/**
 * @brief Determines if file on file descriptor fd is an msz file.
 *        Reads first 512 bytes of file and looks for MAGIC_TAG.
 * 
 * @param fd File descriptor to read.
 * 
 * @return 1 if file is a msz file. 0 otherwise.
 */
{
    char buff [512];
    char magic_buff[4];
    int* magic_buff_cast = (int*)(&magic_buff[0]);
    *magic_buff_cast = MAGIC_TAG;

    size_t rv;

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
/**
 * @brief Determines if file on file descriptor fd is an mzML file.
 *        Reads first 512 bytes of file and looks for substring "indexedmzML".
 * 
 * @param fd File descriptor to read.
 * 
 * @return 1 if file is a mzML file. 0 otherwise.
 */
{
    char buff[512];
    size_t rv;

    rv = read_from_file(fd, (void*)&buff, 512);
    lseek(fd, 0, SEEK_SET);
    
    if(rv != 512)
        return 0;
    
    if(strstr(buff, "indexedmzML") != NULL)
        return 1;

    return 0;
        
}

int
determine_filetype(int fd)
/**
 * @brief Determines what file is on file descriptor fd.
 * 
 * @param fd File descriptor to read.
 * 
 * @return COMPRESS (1) if file is a mzML file.
 *         DECOMPRESS (2) if file is a msz file.
 *         -1 on error.
 */
{
  if(is_mzml(fd))
  {
    print("\t.mzML file detected.\n");
    return COMPRESS;
  }
  else if(is_msz(fd))
  {
    print("\t.msz file detected.\n");
    return DECOMPRESS;
  }  
  else
    error("Invalid input file.\n");
  return -1;

}

char*
change_extension(char* input, char* extension)
/**
 * @brief Changes a path string's extension.
 * 
 * @param input Original path string.
 * 
 * @param extension Desired extension to append. MUST be NULL terminated.
 * 
 * @return Malloc'd char array with new path string.
 */
{
  if(input == NULL)
    error("change_extension: input is NULL.\n");
  if(extension == NULL)
    error("change_extension: extension is NULL.\n");

  char* x;
  char* r;

  r = malloc(sizeof(char) * (strlen(input) + 1));
  if(r == NULL)
    error("change_extension: malloc failed.\n");

  strcpy(r, input);
  x = strrchr(r, '.');
  strcpy(x, extension);

  return r;
}

int
prepare_fds(char* input_path,
            char** output_path,
            char* debug_output,
            char** input_map,
            long* input_filesize,
            int* fds)
/**
 * @brief Prepares all required file descriptors, creates/opens files, determines filetypes, and handles errors.
 * 
 * @param input_path Path of input file.
 * 
 * @param output_path Reference to output path. 
 *                    If empty, output_path will equals input_path with changed extension.
 * 
 * @param debug_output Path of debug dump file. (Optional)
 * 
 * @param input_map Reference to input_map for mmap.
 *                  On success, input_path will be mmap'ed here.
 * 
 * @param input_filesize Reference to input_filesize.
 *                       On success, input_filesize will contain the filesize of input_path.
 * 
 * @param fds An array of (3) file descriptors.
 *            On success, will contain non-negative values.
 *            fds[3] = {input_fd, output_fd, debug_fd}
 * 
 * @return COMPRESS (1) if file is a mzML file.
 *         DECOMPRESS (2) if file is a msz file.
 *         Exit (Errno: 1) on error.
 */
{
  int input_fd;
  int output_fd;
  int type;

  if(input_path)
    input_fd = open(input_path, O_RDONLY);
  else
    error("No input file specified.\n");
  
  if(input_fd < 0)
    error("Error in opening input file descriptor. (%s)\n", strerror(errno));


  if(debug_output)
  {
    fds[2] = open(debug_output, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0666);
    print("\tDEBUG OUTPUT: %s\n", debug_output);
  }

  type = determine_filetype(input_fd);

  if(type != COMPRESS && type != DECOMPRESS)
    error("Cannot determine file type.\n");

  fds[0] = input_fd;
  *input_map = get_mapping(input_fd);
  *input_filesize = get_filesize(input_path);

  if(*output_path)
  {
    output_fd = open(*output_path, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0666);
    if(output_fd < 0)
      error("Error in opening output file descriptor. (%s)\n", strerror(errno));
    fds[1] = output_fd;
    return type;
  }
   

  if(type == COMPRESS)
    *output_path = change_extension(input_path, ".msz\0");
  else if(type == DECOMPRESS)
    *output_path = change_extension(input_path, ".mzML\0");

  output_fd = open(*output_path, O_WRONLY|O_CREAT|O_TRUNC|O_APPEND, 0666);

  if(output_fd < 0)
    error("Error in opening output file descriptor. (%s)\n", strerror(errno));

  fds[1] = output_fd;

  return type;
}

void
close_fd(int fd)
{
    int ret = close(fd);
    if (ret != 0)
    {
        perror("close_fd");
        exit(-1);
    }
}