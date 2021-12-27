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

data_format*
alloc_df()
{
    data_format* df = (data_format*)malloc(sizeof(data_format));
    df->populated = 0;
    return df;
}

data_positions*
alloc_dp(int total_spec)
{
    data_positions* dp = (data_positions*)malloc(sizeof(data_positions));
    dp->total_spec = total_spec;
    dp->file_end = 0;
    dp->start_positions = (int*)malloc(sizeof(int)*total_spec*2);
    dp->end_positions = (int*)malloc(sizeof(int)*total_spec*2);
    dp->positions_len = (int*)malloc(sizeof(int)*total_spec*2);
    return dp;
}

void
free_dp(data_positions* dp)
{
    free(dp->start_positions);
    free(dp->end_positions);
    free(dp);
}

/* === End of allocation and deallocation helper functions === */


/* === Start of XML traversal functions === */

int
map_to_df(int acc, int* current_type, data_format* df)
/**
 * @brief Map a accession number to the data_format struct.
 * This function populates the original compression method, m/z data array format, and 
 * intensity data array format. 
 * 
 * @param acc A parsed integer of an accession attribute. (Expanded by parse_acc_to_int)
 * 
 * @param current_type A pass-by-reference variable to indicate if the traversal is within an m/z or intensity array.
 * 
 * @param df An allocated unpopulated data_format struct to be populated by this function
 * 
 * @return 1 if data_format struct is fully populated, 0 otherwise.
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



data_format*
pattern_detect(char* input_map)
/**
 * @brief Detect the data type and encoding within .mzML file.
 * As the data types and encoding is consistent througout the entire .mzML document,
 * the function stops its traversal once all fields of the data_format struct are filled.
 * 
 * @param input_map A mmap pointer to the .mzML file.
 * 
 * @return A populated data_format struct on success, NULL pointer on failure.
 */
{
    data_format* df = alloc_df();
    
    yxml_t* xml = alloc_yxml();

    char attrbuf[11], *attrcur = NULL, *tmp; /* Length of a accession tag is at most 10 characters, leave room for null terminator. */
    
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

data_positions*
find_binary(char* input_map, data_format* df)
/**
 * @brief Find the position of all binary data within .mzML file using the yxml library traversal.
 * As the file is mmaped, majority of the .mzML will be loaded into memory during this traversal.
 * Thus, the runtime of this function may be significantly slower as there are many disk reads in this step.
 * 
 * @param input_map A mmap pointer to the .mzML file.
 * 
 * @param df A populated data_format struct from pattern_detect(). Use the total_spec field in the struct
 *  to stop the XML traversal once all spectra binary data are found (ignore the chromatogramList)
 *
 * @return A data_positions array populated with starting and ending positions of each data section on success.
 *         NULL pointer on failure.
 */
{
    data_positions* dp = alloc_dp(df->total_spec);

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
    free_dp(dp);
    return NULL;

}


/* === End of XML traversal functions === */

data_positions**
get_binary_divisions(data_positions* dp, int* blocksize, int threads)
{

    data_positions** r;
    int divisions = 0;
    int i = 0;
    int curr_size = 0;
    int curr_div  = 0;
    int curr_div_i = 0;
    
    divisions = dp->file_end/(*blocksize*threads);
    // printf("divisions: %d\n", divisions);
    if(divisions < threads)
    {
        divisions = threads;
        *blocksize = dp->file_end/threads + 1;
        printf("new blocksize: %d\n", *blocksize);
    }

    
    r = (data_positions**)malloc(divisions*sizeof(data_positions*));

    for(i; i < divisions; i++) 
    {
        r[i] = alloc_dp((dp->total_spec*2)/(divisions-1));
        r[i]->total_spec = 0;
    }

    i = 0;

    
    for(i; i < dp->total_spec * 2; i++)
    {
        if(curr_size > *blocksize/2)
        {
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

    for(int x = 0; x<divisions; x++)
    {
        printf("division %d:\n", x);
        for(int y = 0; y<r[x]->total_spec; y++)
        {
            printf("(%d,%d)\n", r[x]->start_positions[y], r[x]->end_positions[y]);
        }
    }

    return r;
}