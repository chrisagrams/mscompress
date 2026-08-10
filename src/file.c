/**
 * @file file.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to provide platform-dependent low level file
 * read/write operations.
 * @version 0.0.1
 * @date 2021-12-21
 *
 * @copyright
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "mscompress.h"

#ifdef _WIN32
#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#define close _close
#define read _read
#define write _write
#define lseek64 _lseeki64
#define ssize_t int
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

/**
 * @brief Maps a file into memory using platform-specific APIs.
 *
 * Implements `mmap()` on POSIX systems and MapViewOfFile on Windows.
 *
 * @param fd File descriptor to be mapped.
 *
 * @return Pointer to the mapped memory region on success. `NULL` on error.
 *
 * @note The returned memory is mmap'd. The caller must release it with
 *       remove_mapping() when done.
 */
void* get_mapping(int fd) {
   int status;

   struct stat buff;

   void* mapped_data = NULL;

   if (fd == -1)
      return NULL;

   status = fstat(fd, &buff);

   if (status == -1)
      return NULL;

// On Windows platform, use MapViewOfFile
#ifdef _WIN32

   HANDLE hFile = (HANDLE)_get_osfhandle(fd);
   HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);

   if (hMapping != NULL) {
      mapped_data = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, buff.st_size);
      CloseHandle(hMapping);
   }

// On POSIX systems, use mmap
#else

   mapped_data = mmap(NULL, buff.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

#endif

   return mapped_data;
}

/**
 * @brief Unmaps a memory-mapped region.
 * @param addr Pointer to the mapped memory region.
 * @param length Length of the mapped memory region.
 *
 * @return int 0 on success, -1 on error.
 */
int remove_mapping(void* addr, size_t length) {
   int result = -1;

#ifdef _WIN32

   result = UnmapViewOfFile(addr);

#else

   result = munmap(addr, length);

#endif

   return result;
}

/**
 * @brief Removes a file from the filesystem. Primarily used for cleanup on
 * error.
 * @param path Path to the file to be removed.
 *
 * @return `int` 0 on success, non-zero on error.
 */
int remove_file(char* path) {
   if (!path) {
      warning("remove_file: Path is NULL\n;");
      return 1;
   }

   int ret = remove(path);

   if (ret != 0)
      perror("remove_file");

   return ret;
}

/**
 * @brief Gets the filesize of a file at path.
 * @param path Path to the file.
 *
 * @return `size_t` Size of the file in bytes. Returns 0 if the path is a
 * directory or an error occurs.
 */
size_t get_filesize(char* path) {
   struct stat fi;

   stat(path, &fi);

#ifdef _WIN32
   if (fi.st_mode & _S_IFDIR)  // Is a directory on Windows
      return 0;
#else
   if (S_ISDIR(fi.st_mode))  // Is a directory
      return 0;
#endif

   return fi.st_size;
}

/**
 * @brief Writes n bytes from buff to file descriptor fd.
 * @param fd File descriptor to write to.
 * @param buff Buffer containing data to write.
 * @param n Number of bytes to write.
 *
 * @return `size_t` Number of bytes actually written.
 */
size_t write_to_file(int fd, char* buff, size_t n) {
   if (fd < 0)
      error("write_to_file: invalid file descriptor.\n");

   ssize_t rv;

#ifdef _WIN32
   rv = write(fd, buff, (unsigned int)n);
#else
   rv = write(fd, buff, n);
#endif

   if (rv < 0)
      error(
          "Error in writing %ld bytes to file descriptor %d. Attempted to "
          "write %s",
          n, fd, buff);

   return (size_t)rv;
}

/**
 * @brief Reads n bytes from file descriptor `fd` into buff.
 * @param fd File descriptor to read from.
 * @param buff Buffer to store the read data.
 * @param n Number of bytes to read.
 *
 * @return `size_t` Number of bytes actually read.
 */
size_t read_from_file(int fd, void* buff, size_t n) {
   if (fd < 0)
      error("read_from_file: invalid file descriptor.\n");

   ssize_t rv;
   size_t total_read = 0;
   char* current_buff = (char*)buff;

   while (total_read < n) {
#ifdef _WIN32
      rv = read(fd, current_buff, (unsigned int)(n - total_read));
#else
      rv = read(fd, current_buff, n - total_read);
#endif

      if (rv < 0) {
         error("Error in reading %ld bytes from file descriptor %d.",
               n - total_read, fd);
      } else if (rv == 0) {
         // End of file reached before reading the required number of bytes.
         warning("End of file reached\n");
         break;
      } else {
         total_read += rv;
         current_buff += rv;
      }
   }

   return total_read;
}

/**
 * @brief Gets the current offset of a file descriptor.
 * @param fd File descriptor whose offset is to be retrieved.
 *
 * @return `long` Current offset of the file descriptor.
 */
long get_offset(int fd) {
#ifdef _WIN32
   return _lseeki64(fd, 0, SEEK_CUR);
#else
   return lseek(fd, 0, SEEK_CUR);
#endif
}

/**
 * @brief Serializes a `data_format_t` struct into a byte buffer.
 *
 * Copies the serializable fields of the `data_format_t` struct into a
 * contiguous calloc'd buffer of `DATA_FORMAT_T_SIZE` bytes.
 *
 * @param df Pointer to the `data_format_t` struct to serialize.
 *
 * @return Pointer to the serialized byte buffer.
 *
 * @note The returned buffer is malloc'd (via calloc). The caller must free it.
 */
char* serialize_df(data_format_t* df) {
   char* r = calloc(1, DATA_FORMAT_T_SIZE);
   if (r == NULL)
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

/**
 * @brief Deserializes a byte buffer into a `data_format_t` struct.
 *
 * Reads serialized fields from the buffer and populates a newly allocated
 * data_format_t struct. Runtime-only fields (function pointers, populated flag)
 * are not set by this function.
 *
 * @param buff Pointer to the byte buffer to deserialize.
 *
 * @return Pointer to the deserialized `data_format_t` struct.
 *
 * @note The returned data_format_t is malloc'd. The caller must free it.
 */
data_format_t* deserialize_df(char* buff) {
   /* Zeroed, not merely allocated: only the serialized members are filled in
    * below, so every runtime-only member must start from a defined value. A
    * caller reading one before set_{compress,decompress}_runtime_variables()
    * has run would otherwise act on whatever happened to be on the heap. */
   data_format_t* r = calloc(1, sizeof(data_format_t));
   if (r == NULL)
      error("deserialize_df: calloc failed.\n");

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

/**
 * @brief Writes the 512-byte .msz header to a file descriptor.
 *
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
 *
 * @param fd File descriptor to write to.
 * @param df Pointer to the `data_format_t` struct containing header information.
 * @param blocksize Blocksize to write to header.
 * @param md5 MD5 checksum string to write to header.
 *
 * @return Number of bytes written (always `HEADER_SIZE`).
 */
size_t write_header(int fd, data_format_t* df, long blocksize, char* md5) {
   // Allocate header_buff
   char header_buff[HEADER_SIZE] = "";

   // Interpret #defines
   char message_buff[MESSAGE_SIZE] = MESSAGE;
   int magic_tag = MAGIC_TAG;
   int format_version_major = FORMAT_VERSION_MAJOR;
   int format_version_minor = FORMAT_VERSION_MINOR;

   memcpy(header_buff, &magic_tag, sizeof(magic_tag));
   memcpy(header_buff + sizeof(magic_tag), &format_version_major,
          sizeof(format_version_major));
   memcpy(header_buff + sizeof(magic_tag) + sizeof(format_version_major),
          &format_version_minor, sizeof(format_version_minor));

   memcpy(header_buff + MESSAGE_OFFSET, message_buff, MESSAGE_SIZE);

   char* df_buff = serialize_df(df);
   memcpy(header_buff + DATA_FORMAT_T_OFFSET, df_buff, DATA_FORMAT_T_SIZE);
   free(df_buff); /* serialize_df returns a malloc'd buffer owned by the caller */

   memcpy(header_buff + BLOCKSIZE_OFFSET, &blocksize, sizeof(blocksize));

   memcpy(header_buff + MD5_OFFSET, md5, MD5_SIZE);

   write_to_file(fd, header_buff, HEADER_SIZE);

   return HEADER_SIZE;
}

/**
 * @brief Reads the blocksize value stored in the MSZ file header.
 *
 * Extracts the blocksize directly from the mmap'd file at the
 * `BLOCKSIZE_OFFSET` position within the 512-byte header.
 *
 * @param input_map Pointer to the mmap'd input file.
 *
 * @return The blocksize value stored in the header.
 */
long get_header_blocksize(void* input_map) {
   long* r;
   r = (long*)((uint8_t*)input_map + BLOCKSIZE_OFFSET);
   return *r;
}

/**
 * @brief Extracts and deserializes the `data_format_t` struct from the MSZ header.
 *
 * Reads the serialized data format information from the header region of the
 * mmap'd input file and returns it as a newly allocated data_format_t struct
 * with the `populated` flag set to 2.
 *
 * @param input_map Pointer to the mmap'd input file.
 *
 * @return Pointer to a newly allocated `data_format_t` struct.
 *
 * @note The returned data_format_t is malloc'd. The caller must free it
 *       (e.g., via `dealloc_df()`).
 */
data_format_t* get_header_df(void* input_map) {
   data_format_t* r;

   r = deserialize_df((char*)((uint8_t*)input_map + DATA_FORMAT_T_OFFSET));

   r->populated = 2;

   return r;
}

/**
 * @brief Writes a `footer_t` struct to a file descriptor.
 *
 * Sets the magic tag on the footer before writing the entire struct
 * as raw bytes to the given file descriptor.
 *
 * @param footer Pointer to the `footer_t` struct to write.
 * @param fd File descriptor to write to.
 *
 * @return Number of bytes written (always `sizeof(footer_t)`).
 */
size_t write_footer(footer_t* footer, int fd) {
   footer->magic_tag = MAGIC_TAG;  // Set magic tag
   write_to_file(fd, (char*)footer, sizeof(footer_t));
   return sizeof(footer_t);
}

/**
 * @brief Reads the `footer_t` struct from the end of an mmap'd MSZ file.
 *
 * The footer is located at the last `sizeof(footer_t)` bytes of the file.
 * Validates the magic tag to ensure the footer is valid.
 *
 * @param input_map Pointer to the mmap'd input file.
 * @param filesize Size of the input file in bytes.
 *
 * @return Pointer to the `footer_t` struct within the mmap'd region, or `NULL` on error.
 *
 * @warning The returned pointer points into the mmap'd region (not malloc'd).
 *          Do NOT free it directly. It is valid only as long as the mapping exists.
 */
footer_t* read_footer(void* input_map, long filesize) {
   footer_t* footer;

   if (filesize <= 0) {
      warning("read_footer: Filesize <= 0.\n");
      return NULL;
   }

   footer = (footer_t*)((char*)input_map + filesize - sizeof(footer_t));

   if (footer->magic_tag != MAGIC_TAG) {
      error("read_footer: invalid magic tag.\n");
      return NULL;
   }

   return footer;
}

/**
 * @brief Prints the contents of a `footer_t` struct to stdout in CSV format.
 *
 * Outputs a CSV header line followed by a single data line containing all
 * footer fields. Useful for debugging and inspection of MSZ file metadata.
 *
 * @param footer Pointer to the `footer_t` struct to print.
 */
void print_footer_csv(footer_t* footer) {
   if (!footer) {
      warning("print_footer_csv: footer is NULL.\n");
      return;
   }

   printf(
       "xml_pos,mz_binary_pos,inten_binary_pos,xml_blk_pos,mz_binary_blk_pos,"
       "inten_binary_blk_pos,divisions_t_pos,num_spectra,original_filesize,n_"
       "divisions,magic_tag,mz_fmt,inten_fmt,mz_shuffle,inten_shuffle\n");
   /* mz_fmt/inten_fmt are reported with the shuffle marker stripped, so the
    * value keeps mapping straight onto an algorithm accession as it always
    * has; the marker is surfaced in its own trailing columns instead. */
   printf("%lu,%lu,%lu,%lu,%lu,%lu,%lu,%zu,%lu,%d,%d,%d,%d,%d,%d\n",
          footer->xml_pos, footer->mz_binary_pos, footer->inten_binary_pos,
          footer->xml_blk_pos, footer->mz_binary_blk_pos,
          footer->inten_binary_blk_pos, footer->divisions_t_pos,
          footer->num_spectra, footer->original_filesize, footer->n_divisions,
          footer->magic_tag, MSZ_ALGO(footer->mz_fmt),
          MSZ_ALGO(footer->inten_fmt), MSZ_HAS_SHUFFLE(footer->mz_fmt) ? 1 : 0,
          MSZ_HAS_SHUFFLE(footer->inten_fmt) ? 1 : 0);
}

void print_footer_json(footer_t* footer) {
   if (!footer) {
      warning("print_footer_json: footer is NULL.\n");
      return;
   }

   printf("{\n");
   printf("  \"xml_pos\": %lu,\n", footer->xml_pos);
   printf("  \"mz_binary_pos\": %lu,\n", footer->mz_binary_pos);
   printf("  \"inten_binary_pos\": %lu,\n", footer->inten_binary_pos);
   printf("  \"xml_blk_pos\": %lu,\n", footer->xml_blk_pos);
   printf("  \"mz_binary_blk_pos\": %lu,\n", footer->mz_binary_blk_pos);
   printf("  \"inten_binary_blk_pos\": %lu,\n", footer->inten_binary_blk_pos);
   printf("  \"divisions_t_pos\": %lu,\n", footer->divisions_t_pos);
   printf("  \"num_spectra\": %zu,\n", footer->num_spectra);
   printf("  \"original_filesize\": %lu,\n", footer->original_filesize);
   printf("  \"n_divisions\": %d,\n", footer->n_divisions);
   printf("  \"magic_tag\": %d,\n", footer->magic_tag);
   printf("  \"mz_fmt\": %d,\n", MSZ_ALGO(footer->mz_fmt));
   printf("  \"inten_fmt\": %d,\n", MSZ_ALGO(footer->inten_fmt));
   printf("  \"mz_shuffle\": %s,\n",
          MSZ_HAS_SHUFFLE(footer->mz_fmt) ? "true" : "false");
   printf("  \"inten_shuffle\": %s\n",
          MSZ_HAS_SHUFFLE(footer->inten_fmt) ? "true" : "false");
   printf("}\n");
}

/**
 * @brief Determines if a memory-mapped file is an MSZ file.
 *
 * Checks whether the first 4 bytes of the file match the `MAGIC_TAG` constant.
 *
 * @param input_map Pointer to the memory-mapped file.
 * @param input_length Length of the memory-mapped file in bytes.
 *
 * @return 1 if the file is an MSZ file, 0 otherwise.
 */
int is_msz(void* input_map, size_t input_length) {
   if (input_length <
       4) {  // If there's less than 4 bytes, it can't match the MAGIC_TAG
      return 0;
   }

   char* mapped_data = (char*)input_map;
   char magic_buff[4];
   int* magic_buff_cast = (int*)(&magic_buff[0]);
   *magic_buff_cast = MAGIC_TAG;

   if (strncmp(mapped_data, magic_buff, 4) == 0)
      return 1;

   return 0;
}

/**
 * @brief Determines if file mapped in input_map is an mzML file.
 *        Reads first 512 bytes of file and looks for the "indexedmzML"
 *        wrapper, the "<mzML" root element, or the mzML namespace URI, so
 *        that both indexed and non-indexed mzML (e.g. Waters exports) are
 *        recognized.
 *
 * @param input_map Pointer to the memory-mapped file.
 *
 * @return 1 if file is a mzML file. 0 otherwise.
 */
int is_mzml(void* input_map, size_t input_length) {
   char buffer[513];  // 512 for data + 1 for null-terminator
   size_t check_length = (input_length > 512) ? 512 : input_length;

   memcpy(buffer, input_map, check_length);
   buffer[check_length] = '\0';  // null-terminate

   if (strstr(buffer, "indexedmzML") != NULL)
      return 1;

   if (strstr(buffer, "<mzML") != NULL)
      return 1;

   if (strstr(buffer, "http://psi.hupo.org/ms/mzml") != NULL)
      return 1;

   return 0;
}

/**
 * @brief Determines what file is in the memory-mapped file.
 *
 * @param input_map Pointer to the memory-mapped file.
 * @param input_length Length of the memory-mapped file.
 *
 * @return `COMPRESS` (1) if file is a mzML file.
 *         `DECOMPRESS` (2) if file is a msz file.
 *         `EXTERNAL` (5) if file is not mzML or msz.
 *         -1 on error.
 */
int determine_filetype(void* input_map, size_t input_length) {
   if (is_mzml(input_map, input_length)) {
      print("\t.mzML file detected.\n");
      return COMPRESS;
   } else if (is_msz(input_map, input_length)) {
      print("\t.msz file detected.\n");
      return DECOMPRESS;
   } else {
      // warning("Invalid input file.\n");
      print("\tExternal file detected.\n");
      return EXTERNAL;
   }
   return -1;
}

/**
 * @brief Replaces the file extension of a path string with a new extension.
 *
 * Finds the last '.' in the input path and replaces everything from that
 * point with the new extension.
 *
 * @param input Original path string.
 * @param extension Desired extension to replace with (e.g., ".msz"). Must be `NULL` terminated.
 *
 * @return New path string with the replaced extension.
 *
 * @note The returned string is malloc'd. The caller must free it.
 * @warning Assumes the input path contains a '.' character (extension separator).
 */
char* change_extension(char* input, char* extension) {
   if (input == NULL)
      error("change_extension: input is NULL.\n");
   if (extension == NULL)
      error("change_extension: extension is NULL.\n");

   char* x;
   char* r;

   r = malloc(sizeof(char) * (strlen(input) + 1));
   if (r == NULL)
      error("change_extension: malloc failed.\n");

   strcpy(r, input);
   x = strrchr(r, '.');
   strcpy(x, extension);

   return r;
}

/**
 * @brief Appends an extension to the end of a path string.
 *
 * Concatenates the extension string to the end of the input path without
 * removing any existing extension.
 *
 * @param input Original path string.
 * @param extension Desired extension to append (e.g., ".msz"). Must be `NULL` terminated.
 *
 * @return New path string with the appended extension.
 *
 * @note The returned string is malloc'd. The caller must free it.
 */
char* append_extension(char* input, char* extension) {
   if (input == NULL)
      error("change_extension: input is NULL.\n");
   if (extension == NULL)
      error("change_extension: extension is NULL.\n");

   char* x;
   char* r;

   r = malloc(sizeof(char) * (strlen(input) + strlen(".msz")));
   if (r == NULL)
      error("change_extension: malloc failed.\n");

   strcpy(r, input);
   x = strrchr(r, '\0');
   strcpy(x, extension);

   return r;
}

/**
 * @brief Strips the ".msz" extension from a path string if present,
 *        otherwise appends ".mzML" to the path string.
 *
 * Used to generate a default output path during decompression.
 *
 * @param input Original path string.
 *
 * @return New path string with the ".msz" extension removed or ".mzML" appended.
 *
 * @note The returned string is malloc'd. The caller must free it.
 */
char* strip_or_append_extension(char* input) {
   if (input == NULL)
      error("strip_or_append_extension: input is NULL.\n");

   char* r;
   char* x;

   r = malloc(sizeof(char) * (strlen(input) + 1));
   if (r == NULL)
      error("strip_or_append_extension: malloc failed.\n");

   strcpy(r, input);
   x = strrchr(r, '.');
   if (x != NULL && strcmp(x, ".msz") == 0)  // If .msz found, strip it
      *x = '\0';
   else  // Otherwise, append .mzML
      strcat(r, ".mzML");

   return r;
}

/**
 * @brief Opens output file at path for writing.
 *        Sets correct flags when opening in Windows to avoid newline
 * translation.
 *
 * @param path Path of output file.
 * @return int File descriptor (integer) on success. Value < 0 on error.
 */
int open_output_file(char* path) {
   int fd = -1;

   if (path) {
#ifdef _WIN32
      fd = _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_APPEND | _O_BINARY,
                 0666);  // open in binary mode to avoid newline translation in
                         // Windows.
#else
      fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
#endif
      if (fd < 0)
         warning("Error in opening output file descriptor. (%s)\n",
                 strerror(errno));
   }

   return fd;
}

/**
 * @brief Closes a file descriptor.
 *
 * @param fd File descriptor to close.
 *
 * @return int 0 on success, non-zero on error.
 */
int close_file(int fd) {
   if (fd == -1)  // File never opened, don't close
      return 0;
   int ret = close(fd);  // expands to _close on Windows
   if (ret != 0) {
      perror("close_file");
      exit(-1);
   }
   return ret;
}

/**
 * @brief Flushes a file descriptor's buffers to disk.
 *
 * @param fd File descriptor to flush.
 *
 * @return int 0 on success, non-zero on error.
 */
int flush(int fd) {
#ifdef _WIN32
   if (_commit(fd) == -1) {
      perror("_commit failed");
      return 1;
   }
#else
   if (fsync(fd) == -1) {
      perror("fsync failed");
      return 1;
   }
#endif

   return 0;
}

/**
 *  @brief Opens input_path read-only to provide file descriptor for input mzML
 * or msz. Sets correct flags when opening in Windows to avoid newline
 * translation.
 *
 *  @param input_path Path of input file.
 *
 *  @return File descriptor (integer) on success. Value < 0 on error.
 */
int open_input_file(char* input_path) {
   int input_fd = -1;

   if (input_path) {
#ifdef _WIN32
      input_fd =
          _open(input_path,
                _O_RDONLY | _O_BINARY);  // open in binary mode to avoid newline
                                         // translation in Windows.
#else
      input_fd = open(input_path, O_RDONLY);
#endif

      if (input_fd < 0)
         warning("Error in opening input file descriptor. (%s)\n",
                 strerror(errno));
   } else {
      warning("No input file specified.\n");
   }
   return input_fd;
}

/**
 * @brief Prepares all required file descriptors, creates/opens files,
 * determines filetypes, and handles errors.
 *
 * @param input_path Path of input file.
 *
 * @param output_path Reference to output path.
 *                    If empty, output_path will equals input_path with changed
 * extension.
 *
 * @param debug_output Path of debug dump file. (Optional)
 *
 * @param input_map Reference to input_map for mmap.
 *                  On success, input_path will be mmap'ed here.
 *
 * @param input_filesize Reference to input_filesize.
 *                       On success, input_filesize will contain the filesize of
 * input_path.
 *
 * @param fds An array of (3) file descriptors.
 *            On success, will contain non-negative values.
 *            `fds[3] = {input_fd, output_fd, debug_fd}`
 *
 * @return `COMPRESS` (1) if file is a mzML file.
 *         `DECOMPRESS` (2) if file is a msz file.
 *         `EXTERNAL` (5) if file is not mzML or msz.
 *         -1 on error.
 */
int prepare_fds(char* input_path, char** output_path, char* debug_output,
                char** input_map, long* input_filesize, int* fds) {
   int input_fd;
   int output_fd;
   int type;

   input_fd = open_input_file(input_path);

   if (input_fd < 0) {
      error("Failed to open input file: %s\n", input_path);
      return -1;
   }

   if (debug_output) {
      fds[2] =
          open(debug_output, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
      print("\tDEBUG OUTPUT: %s\n", debug_output);
   }

   fds[0] = input_fd;
   *input_map = get_mapping(input_fd);
   *input_filesize = get_filesize(input_path);

   if (*input_map == NULL) {
      error("Failed to map input file: %s\n", input_path);
      close_file(input_fd);
      return -1;
   }

   if (*input_filesize <= 0) {
      error("Invalid input file size: %s\n", input_path);
      close_file(input_fd);
      return -1;
   }

   type = determine_filetype(*input_map, *input_filesize);

   if (type != COMPRESS && type != DECOMPRESS && type != EXTERNAL)
      error("Cannot determine file type.\n");

   if (*output_path) {
      output_fd = open_output_file(*output_path);
      fds[1] = output_fd;
      return type;
   }

   if (type == COMPRESS)
      *output_path = change_extension(input_path, ".msz\0");
   else if (type == EXTERNAL)
      *output_path = append_extension(input_path, ".msz\0");
   else if (type == DECOMPRESS)
      *output_path = strip_or_append_extension(input_path);

   output_fd = open_output_file(*output_path);

   if (output_fd < 0)
      error("Error in opening output file descriptor. (%s)\n", strerror(errno));

   fds[1] = output_fd;

   return type;
}

/* ---------- USTAR archive support (used by MSZX) ---------- */

#define TAR_BLOCK_SIZE 512

/* USTAR header field offsets per POSIX.1-1988 */
#define USTAR_NAME_OFF 0
#define USTAR_NAME_LEN 100
#define USTAR_SIZE_OFF 124
#define USTAR_SIZE_LEN 12
#define USTAR_TYPEFLAG_OFF 156
#define USTAR_MAGIC_OFF 257
#define USTAR_MAGIC_LEN 6 /* "ustar\0" */
#define USTAR_PREFIX_OFF 345
#define USTAR_PREFIX_LEN 155

/* Parse the 12-byte octal "size" field. Tar size fields are NUL- or
 * space-terminated octal ASCII; copy to a NUL-terminated buffer first
 * because strtoull on a non-terminated buffer is UB. */
static uint64_t parse_octal(const char* field, size_t len) {
   char buf[24];
   size_t copy = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
   memcpy(buf, field, copy);
   buf[copy] = '\0';
   return (uint64_t)strtoull(buf, NULL, 8);
}

/* Build the full entry name by concatenating the optional 155-byte prefix
 * with the 100-byte name, separated by '/'. Returns a malloc'd NUL-terminated
 * string. Caller must free. */
static char* tar_entry_name(const char* hdr) {
   const char* name = hdr + USTAR_NAME_OFF;
   const char* prefix = hdr + USTAR_PREFIX_OFF;

   size_t name_len = strnlen(name, USTAR_NAME_LEN);
   size_t prefix_len = strnlen(prefix, USTAR_PREFIX_LEN);

   if (prefix_len == 0) {
      char* out = malloc(name_len + 1);
      if (!out) return NULL;
      memcpy(out, name, name_len);
      out[name_len] = '\0';
      return out;
   }

   char* out = malloc(prefix_len + 1 + name_len + 1);
   if (!out) return NULL;
   memcpy(out, prefix, prefix_len);
   out[prefix_len] = '/';
   memcpy(out + prefix_len + 1, name, name_len);
   out[prefix_len + 1 + name_len] = '\0';
   return out;
}

/* Internal walker: iterate ustar headers, invoking cb for each regular-file
 * entry. cb returns 0 to continue, non-zero to stop the walk early.
 *
 * Stops on two consecutive zero blocks (end-of-archive marker), on read EOF,
 * or on the first header missing the ustar magic.
 *
 * Restores the fd position to 0 on return.
 *
 * Returns:
 *   0 on success (full walk completed or cb requested stop)
 *  -1 on I/O error or invalid (non-ustar) archive
 */
typedef int (*tar_entry_cb)(const char* name, int64_t data_offset,
                            size_t data_size, void* user);

static int walk_tar_headers(int fd, tar_entry_cb cb, void* user) {
   char hdr[TAR_BLOCK_SIZE];
   int zero_run = 0;
   int rc = 0;

#ifdef _WIN32
   if (_lseeki64(fd, 0, SEEK_SET) < 0) return -1;
#else
   if (lseek(fd, 0, SEEK_SET) < 0) return -1;
#endif

   for (;;) {
      ssize_t n = read(fd, hdr, TAR_BLOCK_SIZE);
      if (n == 0) break; /* EOF before end-of-archive marker — tolerate */
      if (n != TAR_BLOCK_SIZE) {
         rc = -1;
         break;
      }

      /* Detect zero block (end-of-archive marker is two zero blocks). */
      int is_zero = 1;
      for (size_t i = 0; i < TAR_BLOCK_SIZE; ++i) {
         if (hdr[i] != 0) { is_zero = 0; break; }
      }
      if (is_zero) {
         if (++zero_run >= 2) break;
         continue;
      }
      zero_run = 0;

      /* Validate ustar magic. Empty magic is not ustar — bail. */
      if (memcmp(hdr + USTAR_MAGIC_OFF, "ustar", 5) != 0) {
         rc = -1;
         break;
      }

      uint64_t size = parse_octal(hdr + USTAR_SIZE_OFF, USTAR_SIZE_LEN);
      char typeflag = hdr[USTAR_TYPEFLAG_OFF];

      int64_t data_off;
#ifdef _WIN32
      data_off = _lseeki64(fd, 0, SEEK_CUR);
#else
      data_off = lseek(fd, 0, SEEK_CUR);
#endif
      if (data_off < 0) { rc = -1; break; }

      /* Regular file: typeflag '0' (POSIX) or '\0' (legacy). Pax/GNU
       * extended-header typeflags ('x', 'g', 'L') are not handled — skip
       * them so the search continues to subsequent entries. */
      int is_regular = (typeflag == '0' || typeflag == '\0');

      if (is_regular) {
         char* name = tar_entry_name(hdr);
         if (!name) { rc = -1; break; }
         int stop = cb(name, data_off, (size_t)size, user);
         free(name);
         if (stop) break;
      }

      /* Advance past payload (rounded up to 512). */
      uint64_t padded = (size + (TAR_BLOCK_SIZE - 1)) & ~((uint64_t)(TAR_BLOCK_SIZE - 1));
#ifdef _WIN32
      if (_lseeki64(fd, (int64_t)padded, SEEK_CUR) < 0) { rc = -1; break; }
#else
      if (lseek(fd, (off_t)padded, SEEK_CUR) < 0) { rc = -1; break; }
#endif
   }

#ifdef _WIN32
   _lseeki64(fd, 0, SEEK_SET);
#else
   lseek(fd, 0, SEEK_SET);
#endif
   return rc;
}

struct find_ctx {
   const char* target;
   int64_t offset;
   size_t size;
   int found;
};

static int find_cb(const char* name, int64_t data_offset, size_t data_size,
                   void* user) {
   struct find_ctx* ctx = (struct find_ctx*)user;
   if (strcmp(name, ctx->target) == 0) {
      ctx->offset = data_offset;
      ctx->size = data_size;
      ctx->found = 1;
      return 1; /* stop walk */
   }
   return 0;
}

/**
 * @brief Locate a regular file entry in a USTAR tar archive by name.
 *
 * Walks the tar headers from byte 0, comparing each entry's full name
 * (prefix + '/' + name when prefix is non-empty) against `name`. Stops
 * at the first match.
 *
 * @param fd Open file descriptor for the archive (positioned to anywhere;
 *           restored to 0 on return).
 * @param name Target entry name. Must match exactly.
 * @param out_offset On success, set to the byte offset of the entry's data.
 * @param out_size On success, set to the entry's size in bytes.
 *
 * @return 0 on success. -1 if the entry was not found, or if the archive
 *         is not a valid USTAR tar (no magic).
 */
int find_tar_entry(int fd, const char* name, int64_t* out_offset,
                   size_t* out_size) {
   if (fd < 0 || !name || !out_offset || !out_size) return -1;
   struct find_ctx ctx = {name, 0, 0, 0};
   int rc = walk_tar_headers(fd, find_cb, &ctx);
   if (rc != 0 && !ctx.found) return -1;
   if (!ctx.found) return -1;
   *out_offset = ctx.offset;
   *out_size = ctx.size;
   return 0;
}

/**
 * @brief Iterate every regular-file entry in a USTAR tar via callback.
 *
 * Same walker as find_tar_entry but does not stop on a name match. Callback
 * returns 0 to continue, non-zero to stop early.
 *
 * @return 0 on a clean walk; -1 on I/O error or invalid archive.
 */
int walk_tar_entries(int fd, int (*cb)(const char*, int64_t, size_t, void*),
                     void* user) {
   return walk_tar_headers(fd, cb, user);
}

/**
 * @brief mmap a sub-range of a file with platform-correct alignment.
 *
 * The requested `offset` typically does not satisfy the platform's mmap
 * alignment requirement (4 KiB on POSIX, 64 KiB on Windows). This helper
 * rounds the offset down to the nearest aligned boundary, mmaps from there,
 * and returns the alignment padding via *out_pad. Callers expose
 * `(char*)returned_ptr + *out_pad` to downstream code, and unmap with
 * `remove_mapping(returned_ptr, *out_map_length)`.
 *
 * Fails (returns NULL) when the requested range extends past EOF — does NOT
 * silently clamp, since exposing a truncated MSZ would corrupt downstream
 * footer reads.
 *
 * @param fd Open file descriptor.
 * @param offset Byte offset within the file where the logical region begins.
 * @param length Logical length of the region.
 * @param out_pad On success, set to (offset - aligned_offset). Range 0..gran-1.
 * @param out_map_length On success, set to the actual mapped length
 *                       (out_pad + length).
 *
 * @return Pointer to the mapped region (the aligned base, NOT offset by pad)
 *         on success. NULL on error.
 */
void* get_mapping_range(int fd, int64_t offset, size_t length,
                        size_t* out_pad, size_t* out_map_length) {
   if (fd < 0 || !out_pad || !out_map_length || offset < 0) return NULL;

   /* Determine alignment granularity. */
#ifdef _WIN32
   SYSTEM_INFO si;
   GetSystemInfo(&si);
   uint64_t gran = (uint64_t)si.dwAllocationGranularity;
#else
   long page = sysconf(_SC_PAGE_SIZE);
   if (page <= 0) return NULL;
   uint64_t gran = (uint64_t)page;
#endif

   uint64_t off_u = (uint64_t)offset;
   uint64_t aligned_off = off_u - (off_u % gran);
   uint64_t pad = off_u - aligned_off;
   uint64_t map_length = pad + (uint64_t)length;

   /* Look up the file size to bounds-check before requesting the mapping. */
   struct stat st;
   if (fstat(fd, &st) < 0) return NULL;
   uint64_t file_size = (uint64_t)st.st_size;

   if (aligned_off >= file_size) return NULL;
   uint64_t available = file_size - aligned_off;
   if (map_length > available) {
      /* Tar header claims more bytes than the archive contains. Refuse —
       * silent clamping would expose a truncated MSZ to the parser. */
      return NULL;
   }

   *out_pad = (size_t)pad;
   *out_map_length = (size_t)map_length;

#ifdef _WIN32
   HANDLE hFile = (HANDLE)_get_osfhandle(fd);
   HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
   if (hMapping == NULL) return NULL;
   void* base = MapViewOfFile(hMapping, FILE_MAP_READ,
                              (DWORD)(aligned_off >> 32),
                              (DWORD)(aligned_off & 0xFFFFFFFF),
                              (SIZE_T)map_length);
   CloseHandle(hMapping);
   return base;
#else
   void* base = mmap(NULL, (size_t)map_length, PROT_READ, MAP_PRIVATE, fd,
                     (off_t)aligned_off);
   if (base == MAP_FAILED) return NULL;
   return base;
#endif
}