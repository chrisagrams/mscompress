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
#include <argp.h>
#include <stdbool.h>
#include "vendor/yxml/yxml.h"
#include "vendor/base64/lib/config.h"
#include "vendor/base64/include/libbase64.h"

#define parse_acc_to_int(attrbuff) atoi(attrbuff+3)     /* Convert an accession to an integer by removing 'MS:' substring and calling atoi() */

/* === Start of allocation and deallocation helper functions === */

yxml_t*
alloc_yxml()
{
    yxml_t* xml = (yxml_t*)malloc(sizeof(yxml_t) + BUFSIZE);
    yxml_init(xml, xml+1, BUFSIZE);
    return xml;
}

data_format_t*
alloc_df()
{
    data_format_t* df = (data_format_t*)malloc(sizeof(data_format_t));
    df->populated = 0;
    return df;
}

data_positions_t*
alloc_dp(int total_spec)
{
    data_positions_t* dp = (data_positions_t*)malloc(sizeof(data_positions_t));
    dp->total_spec = total_spec;
    dp->file_end = 0;
    dp->start_positions = (off_t*)malloc(sizeof(off_t)*total_spec*2);
    dp->end_positions = (off_t*)malloc(sizeof(off_t)*total_spec*2);
    dp->positions_len = (off_t*)malloc(sizeof(off_t)*total_spec*2);
    return dp;
}

void
dealloc_dp(data_positions_t* dp)
{
    if(dp)
    {
        free(dp->start_positions);
        free(dp->end_positions);
        free(dp);
    }
}


data_positions_t**
alloc_ddp(int len, int total_spec)
{
    data_positions_t** r;
    
    int i;

    r = (data_positions_t**)malloc(len*sizeof(data_positions_t*));

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

    if(ddp)
    {
        i = 0;
        for(i; i < divisions; i++)
            dealloc_dp(ddp[i]);

        free(ddp);
    }

}

/* === End of allocation and deallocation helper functions === */


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
                df->compression = _zlib_;
                break;
            case _no_comp_:
                df->compression = _no_comp_;
                break;
            default:
                if(acc >= _32i_ && acc <= _64d_) {
                    if (*current_type == _intensity_) 
                    {
                        df->int_fmt = acc;
                        df->populated++;
                    }
                    else if (*current_type == _mass_) 
                    {
                        df->mz_fmt = acc;
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
                    df->total_spec = atoi(attrbuf);
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
    data_positions_t* dp = alloc_dp(df->total_spec);

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
                    if (spec_index > dp->total_spec * 2)
                    {
                        printf("\tDetected %d spectra.\n", df->total_spec);
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


/* === End of XML traversal functions === */

data_positions_t**
get_binary_divisions(data_positions_t* dp, long* blocksize, int* divisions, int threads)
{

    data_positions_t** r;
    int i = 0;
    long curr_size = 0;
    int curr_div  = 0;
    int curr_div_i = 0;
    
    if(*divisions == 0)
        *divisions = dp->file_end/(*blocksize*threads);

    if(*divisions < threads && threads > 0)
    {
        *divisions = threads;
        *blocksize = dp->file_end/threads + 1;
        printf("\tUsing new blocksize: %ld bytes.\n", *blocksize);
    }

    printf("\tUsing %d divisions over %d threads.\n", *divisions, threads);

    r = alloc_ddp(*divisions, (dp->total_spec*2)/(*divisions-1));

    i = 0;

    printf("\t=== Divisions distribution (bytes%%/spec%%) ===\n\t");
    
    for(; i < dp->total_spec * 2; i++)
    {
        if(curr_size > *blocksize/2)
        {
            printf("(%2.4f%%/%2.2f%%) ", (double)curr_size/dp->file_end*100,(double)(r[curr_div]->total_spec)/dp->total_spec*100);
            curr_div++;
            curr_div_i = 0;
            curr_size = 0;
        }
        r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
        r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
        r[curr_div]->total_spec++;
        curr_size += dp->end_positions[i]-dp->start_positions[i];
        curr_div_i++;
    }

    printf("\n");
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
            r[curr_div]->end_positions[curr_div_i-1] = binary_divisions[curr_div+1]->start_positions[0];
            curr_div++;

            r[curr_div]->start_positions[0] = binary_divisions[curr_div]->end_positions[0];
            r[curr_div]->end_positions[0] = binary_divisions[curr_div]->start_positions[1];
            r[curr_div]->total_spec++;
            curr_div_i = 1;
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


    // i = 0;
    // for(i; i < divisions; i++)
    // {
    //     printf("division %d\n", i);
    //     for(int j = 0; j < r[i]->total_spec; j++)
    //     {
    //         printf("(%d, %d)\n", r[i]->start_positions[j], r[i]->end_positions[j]);
    //     }
    // }
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
read_dp(void* input_map, long dp_position, long eof)
{
    data_positions_t* r;
    int size;
    long diff;
    int i;
    int j;

    diff = eof - dp_position;
    size = diff / (sizeof(off_t)*2*2);

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