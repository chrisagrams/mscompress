/**
 * @file preprocess.c
 * @author Chris Grams (chrisagrams@gmail.com)
 * @brief A collection of functions to prepare the software and preprocess input files for compression/decompression.
 * @version 0.0.1
 * @date 2021-12-21
 * 
 * @copyright 
 * 
 */

#include "mscompress.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <argp.h>
#include <stdbool.h>
#include <sys/time.h>
#include "vendor/yxml/yxml.h"
#include "vendor/base64/lib/config.h"
#include "vendor/base64/include/libbase64.h"

#define parse_acc_to_int(attrbuff) atoi(attrbuff+3)     /* Convert an accession to an integer by removing 'MS:' substring and calling atoi() */

/* === Start of allocation and deallocation helper functions === */

yxml_t*
alloc_yxml()
{
    yxml_t* xml = (yxml_t*)malloc(sizeof(yxml_t) + BUFSIZE);

    if(!xml)
    {
        fprintf(stderr, "malloc failure alloc_yxml().\n");
        exit(-1);
    }

    yxml_init(xml, xml+1, BUFSIZE);
    return xml;
}

data_format_t*
alloc_df()
{
    data_format_t* df = (data_format_t*)malloc(sizeof(data_format_t));

    if(!df)
    {
        fprintf(stderr, "malloc failure in alloc_df().\n");
        exit(-1);
    }

    df->populated = 0;
    return df;
}

void
dealloc_df(data_format_t* df)
{
    if(df)
        free(df);
    else
    {
        fprintf(stderr, "delloc_df received null.\n");
        exit(-1);
    }
}


void
populate_df_target(data_format_t* df, int target_xml_format, int target_mz_format, int target_int_format)
{
    df->target_xml_format = target_xml_format;
    df->target_mz_format = target_mz_format;
    df->target_int_format = target_int_format;

    df->target_xml_fun = map_fun(target_xml_format);
    df->target_mz_fun = map_fun(target_mz_format);
    df->target_int_fun = map_fun(target_int_format);
}

data_positions_t*
alloc_dp(int total_spec)
{
    data_positions_t* dp = (data_positions_t*)malloc(sizeof(data_positions_t));

    if(!dp)
    {
        fprintf(stderr, "malloc failure in alloc_dp().\n");
        exit(-1);
    }

    dp->total_spec = total_spec;
    dp->file_end = 0;
    dp->start_positions = (off_t*)malloc(sizeof(off_t)*total_spec*2);
    dp->end_positions = (off_t*)malloc(sizeof(off_t)*total_spec*2);

    if(!dp->start_positions || !dp->end_positions)
    {
        fprintf(stderr, "inner malloc failure in alloc_dp().\n");
        exit(-1);
    }

    return dp;
}

void
dealloc_dp(data_positions_t* dp)
{
    if(dp)
    {
        if(dp->start_positions)
            free(dp->start_positions);
        else
        {
            fprintf(stderr, "dp->start_positions is null.\n");
            exit(-1);
        }
        if(dp->end_positions)
            free(dp->end_positions);
        else
        {
            fprintf(stderr, "dp->end_positions is null.\n");
            exit(-1);
        }
        free(dp);
    }
    else
    {
        fprintf(stderr, "dealloc_dp() received null pointer.\n");
        exit(-1);
    }
}

data_positions_t**
alloc_ddp(int len, int total_spec)
{
    data_positions_t** r;
    
    int i;

    r = (data_positions_t**)malloc(len*sizeof(data_positions_t*));

    if(!r)
    {
        fprintf(stderr, "malloc failure in alloc_ddp().\n");
        exit(-1);
    }

    i = 0;

    for(i; i < len; i++)
    {
        r[i] = alloc_dp(total_spec);
        r[i]->total_spec = 0;
    }

    return r;
}

void
free_ddp(data_positions_t** ddp, int divisions)
{
    int i;

    if (divisions < 1)
    {
        fprintf(stderr, "invalid divisions in free_dp().\n");
        exit(-1);
    }

    if(ddp)
    {
        i = 0;
        for(i; i < divisions; i++)
            dealloc_dp(ddp[i]);

        free(ddp);
    }
    else
    {
        fprintf(stderr, "free_dp() received null pointer.\n");
        exit(-1);
    }

}

/* === End of allocation and deallocation helper functions === */
 
char* fun1(void* args)
{
    return "Hello world";
}
 
Algo map_fun(int fun_num)
{
    switch(fun_num)
    {
        case _ZSTD_compression_:
            return &fun1;
        default:
            fprintf(stderr, "Target format not implemented in this version of mscompress.\n");
            exit(-1);
    }
}

/* === Start of XML traversal functions === */

int
map_to_df(int acc, int* current_type, data_format_t* df)
/**
 * @brief Map a accession number to the data_format_t struct.
 * This function populates the original compression method, m/z data array format, and 
 * intensity data array format. 
 * 
 * @param acc A parsed integer of an accession attribute. (Expanded by parse_acc_to_int)
 * 
 * @param current_type A pass-by-reference variable to indicate if the traversal is within an m/z or intensity array.
 * 
 * @param df An allocated unpopulated data_format_t struct to be populated by this function
 * 
 * @return 1 if data_format_t struct is fully populated, 0 otherwise.
 */
{
    if (acc >= _mass_ && acc <= _no_comp_) 
    {
        switch (acc) 
        {
            case _intensity_:
                *current_type = _intensity_;
                break;
            case _mass_:
                *current_type = _mass_;
                break;
            case _zlib_:
                df->source_compression = _zlib_;
                break;
            case _no_comp_:
                df->source_compression = _no_comp_;
                break;
            default:
                if(acc >= _32i_ && acc <= _64d_) {
                    if (*current_type == _intensity_) 
                    {
                        df->source_int_fmt = acc;
                        df->populated++;
                    }
                    else if (*current_type == _mass_) 
                    {
                        df->source_mz_fmt = acc;
                        df->populated++;
                    }
                    if (df->populated >= 2)
                    {
                        return 1;
                    }
                }
                break;
        }
    }
    return 0;

}



data_format_t*
pattern_detect(char* input_map)
/**
 * @brief Detect the data type and encoding within .mzML file.
 * As the data types and encoding is consistent througout the entire .mzML document,
 * the function stops its traversal once all fields of the data_format_t struct are filled.
 * 
 * @param input_map A mmap pointer to the .mzML file.
 * 
 * @return A populated data_format_t struct on success, NULL pointer on failure.
 */
{
    data_format_t* df = alloc_df();
    
    yxml_t* xml = alloc_yxml();

    char attrbuf[11] = {NULL}, *attrcur = NULL, *tmp = NULL; /* Length of a accession tag is at most 10 characters, leave room for null terminator. */
    
    int in_cvParam = 0;                      /* Boolean representing if currently inside of cvParam tag. */
    int current_type = 0;                    /* A pass-by-reference variable to indicate to map_to_df of current binary data array type (m/z or intensity) */

    for(; *input_map; input_map++)
    {
        yxml_ret_t r = yxml_parse(xml, *input_map);
        if(r < 0)
        {
            free(xml);
            free(df);
            return NULL;
        }
        switch(r) 
        {
            case YXML_ELEMSTART:
                if(strcmp(xml->elem, "cvParam") == 0)
                    in_cvParam = 1;
                break;
                    
            case YXML_ELEMEND:
                if(strcmp(xml->elem, "cvParam") == 0)
                    in_cvParam = 0;
                break;
            
            case YXML_ATTRSTART:
                if(in_cvParam && strcmp(xml->attr, "accession") == 0)
                    attrcur = attrbuf;
                else if(strcmp(xml->attr, "count") == 0)
                    attrcur = attrbuf;
                break;

            case YXML_ATTRVAL:
                if(!in_cvParam || !attrcur)
                    break;
                tmp = xml->data;
                while (*tmp && attrcur < attrbuf+sizeof(attrbuf))
                    *(attrcur++) = *(tmp++);
                if(attrcur == attrbuf+sizeof(attrbuf))
                {
                    free(xml);
                    free(df);
                    return NULL;
                }
                *attrcur = 0;
                break;

            case YXML_ATTREND:
                if(attrcur && (strcmp(xml->elem, "spectrumList") == 0) && (strcmp(xml->attr, "count") == 0))
                {
                    df->source_total_spec = atoi(attrbuf);
                    attrcur = NULL;
                }
                else if(in_cvParam && attrcur) 
                {
                    if (map_to_df(parse_acc_to_int(attrbuf), &current_type, df))
                    {
                        free(xml);
                        return df;
                    }
                    attrcur = NULL;
                }
                break;
            
            default:
                /* TODO: handle errors. */
                break;   
        }
    }
    free(xml);
    free(df);
    return NULL;
}

data_positions_t*
find_binary(char* input_map, data_format_t* df)
/**
 * @brief Find the position of all binary data within .mzML file using the yxml library traversal.
 * As the file is mmaped, majority of the .mzML will be loaded into memory during this traversal.
 * Thus, the runtime of this function may be significantly slower as there are many disk reads in this step.
 * 
 * @param input_map A mmap pointer to the .mzML file.
 * 
 * @param df A populated data_format_t struct from pattern_detect(). Use the total_spec field in the struct
 *  to stop the XML traversal once all spectra binary data are found (ignore the chromatogramList)
 *
 * @return A data_positions_t array populated with starting and ending positions of each data section on success.
 *         NULL pointer on failure.
 */
{
    data_positions_t* dp = alloc_dp(df->source_total_spec);

    yxml_t* xml = alloc_yxml();

    int spec_index = 0;                         /* Current spectra index in the traversal */
    int in_binary = 0;                          /* Boolean representing if in a <binary> tag or not. Ignore events if not in <binary> tag. */

    int tag_offset = strlen("</binary>");       /*  
                                                    yxml triggers an event once it finishes processing a tag.
                                                    Discard the </binary> tag from the current position by
                                                    subtracting tag_offset
                                                */

    for(; *input_map; input_map++)
    {
        yxml_ret_t r = yxml_parse(xml, *input_map);
        if(r < 0)   /* Critical error */
        {
            free(xml);
            free(dp);
            return NULL;
        }

        switch(r)
        {
            case YXML_ELEMSTART:
                if(strcmp(xml->elem, "binary") == 0)
                {
                    dp->start_positions[spec_index] = xml->total;
                    in_binary = 1;
                }
                else
                    in_binary = 0;
                break;
            
            case YXML_ELEMEND:
                if(in_binary)
                {
                    dp->end_positions[spec_index] = xml->total - tag_offset;
                    spec_index++;
                    if (spec_index >= dp->total_spec * 2)
                    {
                        print("\tDetected %d spectra.\n", df->source_total_spec);
                        free(xml);
                        return dp;
                    }
                    in_binary = 0;
                }
                break;

            default:
                break;
        }
    }
    dealloc_dp(dp);
    return NULL;

}


data_positions_t*
find_binary_quick(char* input_map, data_format_t* df)
{
    data_positions_t* dp = alloc_dp(df->source_total_spec);

    char* ptr = input_map;

    int curr = 0; 

    int curr_scan = 0;
    int curr_ms_level = 0;

    char* e;

    int bound = df->source_total_spec * 2;

    while (ptr)
    {
        if(curr == bound)
            break;
        ptr = strstr(ptr, "scan=") + 5;
        e = strstr(ptr, "\"");
        curr_scan = strtol(ptr, &e, 10);

        ptr = e;

        ptr = strstr(ptr, "\"ms level\"") + 18;
        e = strstr(ptr, "\"");
        curr_ms_level = strtol(ptr, &e, 10);

        ptr = e;


        ptr = strstr(ptr, "<binary>") + 8;
        dp->start_positions[curr] = ptr - input_map;

        
        ptr = strstr(ptr, "</binary>");
        dp->end_positions[curr] = ptr - input_map;

        // scan_nums[curr] = curr_scan;
        // ms_levels[curr++] = curr_ms_level;
        curr++;


        ptr = strstr(ptr, "<binary>") + 8;
        dp->start_positions[curr] = ptr - input_map;

        
        ptr = strstr(ptr, "</binary>");
        dp->end_positions[curr] = ptr - input_map;

        // scan_nums[curr] = curr_scan;
        // ms_levels[curr++] = curr_ms_level;
        curr++;
    
    }

    dp->total_spec = df->source_total_spec;

    return dp;
}



long
encodedLength_sum(data_positions_t* dp)
{
    int i = 0;
    long res = 0;

    for(; i < dp->total_spec * 2; i++)
        res += dp->end_positions[i]-dp->start_positions[i];
    
    return res;
}


/* === End of XML traversal functions === */

data_positions_t**
get_binary_divisions(data_positions_t* dp, long* blocksize, int* divisions, int* threads)
{

    data_positions_t** r;
    int i = 0;
    long curr_size = 0;
    int curr_div  = 0;
    int curr_div_i = 0;
    
    if(*divisions == 0)
        *divisions = ceil((((double)dp->file_end)/(*blocksize)));

    if(*divisions < *threads && *threads > 0)
    {
        *divisions = *threads;
        *blocksize = dp->file_end/(*threads);
        print("\tUsing new blocksize: %ld bytes.\n", *blocksize);
    }

    print("\tUsing %d divisions over %d threads.\n", *divisions, *threads);

    r = alloc_ddp(*divisions, (dp->total_spec*2));

    i = 0;

    print("\t=== Divisions distribution (bytes%%/spec%%) ===\n\t");
    
    for(; i < dp->total_spec * 2; i++)
    {
        if(curr_size > (*blocksize))
        {
            print("(%2.4f%%/%2.2f%%) ", (double)curr_size/dp->file_end*100,(double)(r[curr_div]->total_spec)/dp->total_spec*100);
            curr_div++;
            curr_div_i = 0;
            curr_size = 0;
        }
        r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
        r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
        r[curr_div]->total_spec++;
        curr_size += (dp->end_positions[i] - dp->start_positions[i]);
        curr_div_i++;
    }

    print("(%2.4f%%/%2.2f%%) ", (double)curr_size/dp->file_end*100,(double)(r[curr_div]->total_spec)/dp->total_spec*100);
    print("\n");

    if(curr_div != *divisions)
        *divisions = curr_div + 1;
    
    if (*divisions < *threads)
    {
        *threads = *divisions;
        print("\tNEW: Using %d divisions over %d threads.\n", *divisions, *threads);
        *blocksize = dp->file_end/(*threads);
        print("\tUsing new blocksize: %ld bytes.\n", *blocksize);
    }

    return r;
}

data_positions_t**
new_get_binary_divisions(data_positions_t* dp, long* blocksize, int* divisions, int* threads)
{
    data_positions_t** r;

    int i = 0;
    int curr_div = 0;
    int curr_div_i = 0;
    int curr_size = 0;

    *divisions = *threads;

    r = alloc_ddp(*divisions, dp->total_spec);

    long encoded_sum = encodedLength_sum(dp);

    *blocksize = encoded_sum/(*divisions);

    int bound = dp->total_spec * 2;

    for(i; i < bound; i++)
    {
        if(curr_size >= *blocksize)
        {
            curr_div++;
            curr_div_i = 0;
            curr_size = 0;
            if(curr_div > *divisions)
                break;
        }
        r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
        r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
        r[curr_div]->total_spec++;
        curr_size += (dp->end_positions[i] - dp->start_positions[i]);
        curr_div_i++;
    }

    curr_div--;

    /* add the remainder to the last division */
    for(i; i < bound; i++)
    {
        r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
        r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
        r[curr_div]->total_spec++;
    }

    return r;
}

data_positions_t**
get_xml_divisions(data_positions_t* dp, data_positions_t** binary_divisions, int divisions)
{
    data_positions_t** r;
    
    int i;
    int curr_div  = 0;
    int curr_div_i = 0;
    int curr_bin_i = 0;

    r = alloc_ddp(divisions, dp->total_spec);

    /* base case */

    r[curr_div]->start_positions[curr_div_i] = 0;
    r[curr_div]->end_positions[curr_div_i] = binary_divisions[0]->start_positions[0];
    r[curr_div]->total_spec++;
    r[curr_div]->file_end = dp->file_end;
    curr_div_i++;
    curr_bin_i++;

    /* inductive step */

    i = 0;

    while(i < dp->total_spec * 2)
    {
        if(curr_bin_i > binary_divisions[curr_div]->total_spec - 1)
        {
            if(curr_div + 1 == divisions)
                break;
            r[curr_div]->file_end = dp->file_end;
            r[curr_div]->end_positions[curr_div_i-1] = binary_divisions[curr_div+1]->start_positions[0];
            curr_div++;
            
            /* First xml division of 0 length, start binary first */
            r[curr_div]->start_positions[0] = binary_divisions[curr_div]->end_positions[0];
            r[curr_div]->end_positions[0] = binary_divisions[curr_div]->end_positions[0];
            r[curr_div]->total_spec++;

            r[curr_div]->start_positions[1] = binary_divisions[curr_div]->end_positions[0];
            r[curr_div]->end_positions[1] = binary_divisions[curr_div]->start_positions[1];
            r[curr_div]->total_spec++;
            curr_div_i = 2;
            curr_bin_i = 2;
        }
        else
        {
            r[curr_div]->start_positions[curr_div_i] = binary_divisions[curr_div]->end_positions[curr_bin_i-1];
            r[curr_div]->end_positions[curr_div_i] = binary_divisions[curr_div]->start_positions[curr_bin_i];
            r[curr_div]->total_spec++;
            curr_div_i++;
            curr_bin_i++;
            i++;
        }   
    }

    /* end case */
    r[curr_div]->start_positions[curr_div_i] = binary_divisions[curr_div]->end_positions[curr_bin_i-1];
    r[curr_div]->end_positions[curr_div_i] = dp->file_end;
    r[curr_div]->total_spec++;
    r[curr_div]->file_end = dp->file_end;

    return r;
}

void
dump_dp(data_positions_t* dp, int fd)
{
    char buff[sizeof(off_t)];
    off_t* buff_cast = (off_t*)(&buff[0]);
    int i;

    for(i = 0; i < dp->total_spec * 2; i++)
    {
        *buff_cast = dp->start_positions[i];
        write_to_file(fd, buff, sizeof(off_t));
        *buff_cast = dp->end_positions[i];
        write_to_file(fd, buff, sizeof(off_t));
    }

    return;
}

data_positions_t*
read_dp(void* input_map, long dp_position, size_t num_spectra, long eof)
{
    data_positions_t* r;
    int size;
    long diff;
    int i;
    int j;

    diff = eof - dp_position;
    size = num_spectra;

    r = alloc_dp(size);

    j = dp_position;

    for(i = 0; i < size * 2; i++)
    {
        r->start_positions[i] = *(off_t*)(&input_map[0]+j);
        j+=sizeof(off_t);
        r->end_positions[i] = *(off_t*)(&input_map[0]+j);
        j+=sizeof(off_t);
    }

    return r;

}

int
preprocess_mzml(char* input_map,
                long input_filesize,
                int* divisions,
                long* blocksize,
                long* n_threads,
                data_positions_t** dp,
                data_format_t** df,
                data_positions_t*** binary_divisions,
                data_positions_t*** xml_divisions)
{
  struct timeval start, stop;

  gettimeofday(&start, NULL);

  print("\nPreprocessing...\n");

  *df = pattern_detect((char*)input_map);

  if (*df == NULL)
      return -1;

//   *dp = find_binary((char*)input_map, *df);
  *dp = find_binary_quick((char*)input_map, *df);

  if (*dp == NULL)
      return -1;

  (*dp)->file_end = input_filesize;

//   *binary_divisions = get_binary_divisions(*dp, blocksize, divisions, n_threads);
  *binary_divisions = new_get_binary_divisions(*dp, blocksize, divisions, n_threads);

//   long sum = encodedLength_sum(*dp);
// 
//   print("\tencodedLengths sum: %ld\n", sum);

  *xml_divisions = get_xml_divisions(*dp, *binary_divisions, *divisions);

  gettimeofday(&stop, NULL);

  print("Preprocessing time: %1.4fs\n", (stop.tv_sec-start.tv_sec)+((stop.tv_usec-start.tv_usec)/1e+6));  

}