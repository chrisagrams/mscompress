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
    yxml_t* xml = malloc(sizeof(yxml_t) + BUFSIZE);

    if(xml == NULL)
        error("malloc failure in alloc_yxml().\n");

    yxml_init(xml, xml+1, BUFSIZE);
    return xml;
}

data_format_t*
alloc_df()
{
    data_format_t* df = malloc(sizeof(data_format_t));

    if(df == NULL)
        error("alloc_df: malloc failure.\n");
    df->populated = 0;
    return df;
}

void
dealloc_df(data_format_t* df)
{
    if(df)
        free(df);
    else
        error("dealloc_df: df is NULL.\n");
}


void
populate_df_target(data_format_t* df, int target_xml_format, int target_mz_format, int target_inten_format)
{
    df->target_xml_format = target_xml_format;
    df->target_mz_format = target_mz_format;
    df->target_inten_format = target_inten_format;

    // df->target_xml_fun = map_fun(target_xml_format);
    // df->target_mz_fun = map_fun(target_mz_format);
    // df->target_inten_fun = map_fun(target_inten_format);
}

data_positions_t*
alloc_dp(int total_spec)
{
    if(total_spec < 0)
        error("alloc_dp: total_spec is less than 0!\n");
    if(total_spec > 1000000) // Realistically, this should never happen
        warning("alloc_dp: total_spec is greater than 1,000,000!\n");
    if(total_spec < 0)
        error("alloc_dp: total_spec is less than 0!\n");  

    data_positions_t* dp = malloc(sizeof(data_positions_t));

    if(dp == NULL)
        error("alloc_dp: malloc failure.\n");

    if(total_spec == 0)
    {
        dp->total_spec = 0;
        dp->file_end = 0;
        dp->start_positions = NULL;
        dp->end_positions = NULL;
    }

    dp->total_spec = total_spec;
    dp->file_end = 0;
    dp->start_positions = malloc(sizeof(off_t)*total_spec*2);
    dp->end_positions = malloc(sizeof(off_t)*total_spec*2);

    if(dp->start_positions == NULL || dp->end_positions == NULL)
        error("alloc_dp: malloc failure.\n");

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
            error("dealloc_dp: dp->start_positions is null.\n");
        if(dp->end_positions)
            free(dp->end_positions);
        else
            error("dealloc_dp: dp->end_positions is null.\n");
        free(dp);
    }
    else
        error("dealloc_dp: dp is null.\n");
}

data_positions_t**
alloc_ddp(int len, int total_spec)
{
    if(len < 1)
        error("alloc_ddp: len is less than 1.\n");
    if(total_spec < 1)
        error("alloc_ddp: total_spec is less than 1.\n");
    if(total_spec > 1000000) // Realistically, this should never happen
        warning("alloc_ddp: total_spec is greater than 1,000,000!\n");

    data_positions_t** r;
    
    int i;

    r = malloc(len*sizeof(data_positions_t*));

    if(r == NULL)
        error("alloc_ddp: malloc failure.\n");

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
        error("free_ddp: divisions is less than 1.\n");

    if(ddp)
    {
        i = 0;
        for(i; i < divisions; i++)
            dealloc_dp(ddp[i]);

        free(ddp);
    }
    else
        error("free_ddp: ddp is null.\n");

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
                df->source_compression = _zlib_;
                break;
            case _no_comp_:
                df->source_compression = _no_comp_;
                break;
            default:
                if(acc >= _32i_ && acc <= _64d_) {
                    if (*current_type == _intensity_) 
                    {
                        df->source_inten_fmt = acc;
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

void
validate_positions(off_t* arr, int len)
{
    int i;
    for(i = 0; i < len; i++)
    {
        if(arr[i] < 0)
            error("validate_positions: negative position detected.\n");
        if(i+1 < len && arr[i] > arr[i+1])
            error("validate_positions: position %d is greater than %d\n", i, i+1);
    }
}


division_t*
find_binary_quick(char* input_map, data_format_t* df, long end)
{
    if(input_map == NULL || df == NULL)
        error("find_binary_quick: NULL pointer passed in.\n");
    if(end < 0)
        error("find_binary_quick: end position is negative.\n");

    data_positions_t *mz_dp, *inten_dp, *xml_dp;

    xml_dp = alloc_dp(df->source_total_spec * 2);
    mz_dp = alloc_dp(df->source_total_spec);
    inten_dp = alloc_dp(df->source_total_spec);

    if(xml_dp == NULL || mz_dp == NULL || inten_dp == NULL)
        error("find_binary_quick: failed to allocate memory.\n");

    char* ptr = input_map;

    int mz_curr = 0, inten_curr = 0, xml_curr = 0;

    int curr_scan = 0;
    int curr_ms_level = 0;

    char* e;

    int bound = df->source_total_spec * 2;

    // xml base case
    xml_dp->start_positions[xml_curr] = 0;

    while (ptr)
    {   
        if(mz_curr + inten_curr == bound)
            break;
        
        if(xml_curr >= bound || mz_curr >= df->source_total_spec || inten_curr >= df->source_total_spec) // We cannot continue if we have reached the end of the array
            error("find_binary_quick: index out of bounds. xml_curr: %d, mz_curr: %d, inten_curr: %d\n", xml_curr, mz_curr, inten_curr);
            
        ptr = strstr(ptr, "scan=") + 5;

        if(ptr == NULL)
            error("find_binary_quick: failed to find scan number. index: %d\n", mz_curr + inten_curr);

        e = strstr(ptr, "\"");

        if(e == NULL)
            error("find_binary_quick: failed to find scan number. index: %d\n", mz_curr + inten_curr);

        curr_scan = strtol(ptr, &e, 10);

        if(curr_scan == 0)
            error("find_binary_quick: failed to find scan number. index: %d\n", mz_curr + inten_curr);

        ptr = e;

        ptr = strstr(ptr, "\"ms level\"") + 18;

        if(ptr == NULL)
            error("find_binary_quick: failed to find ms level. index: %d\n", mz_curr + inten_curr);
        e = strstr(ptr, "\"");

        if(e == NULL)
            error("find_binary_quick: failed to find ms level. index: %d\n", mz_curr + inten_curr);
        curr_ms_level = strtol(ptr, &e, 10);

        ptr = e;


        ptr = strstr(ptr, "<binary>") + 8;
        if(ptr == NULL)
            error("find_binary_quick: failed to find start of binary. index: %d\n", mz_curr + inten_curr);
        mz_dp->start_positions[mz_curr] = ptr - input_map;
        xml_dp->end_positions[xml_curr++] = mz_dp->start_positions[mz_curr];


        
        ptr = strstr(ptr, "</binary>");
        if(ptr == NULL)
            error("find_binary_quick: failed to find end of binary. index: %d\n", mz_curr + inten_curr);
        mz_dp->end_positions[mz_curr] = ptr - input_map;
        xml_dp->start_positions[xml_curr] = mz_dp->end_positions[mz_curr];

        // scan_nums[curr] = curr_scan;
        // ms_levels[curr++] = curr_ms_level;
        mz_curr++;


        ptr = strstr(ptr, "<binary>") + 8;
        if(ptr == NULL)
            error("find_binary_quick: failed to find start of binary. index: %d\n", mz_curr + inten_curr);
        inten_dp->start_positions[inten_curr] = ptr - input_map;
        xml_dp->end_positions[xml_curr++] = inten_dp->start_positions[inten_curr];

        
        ptr = strstr(ptr, "</binary>");
        if(ptr == NULL)
            error("find_binary_quick: failed to find end of binary. index: %d\n", mz_curr + inten_curr);
        inten_dp->end_positions[inten_curr] = ptr - input_map;
        xml_dp->start_positions[xml_curr] = inten_dp->end_positions[inten_curr];
        
        // scan_nums[curr] = curr_scan;
        // ms_levels[curr++] = curr_ms_level;
        inten_curr++;
    
    }

    if(xml_curr != bound || mz_curr != df->source_total_spec || inten_curr != df->source_total_spec) // If we haven't found all the binary data, we have a problem
        error("find_binary_quick: did not find all binary data. xml_curr: %d, mz_curr: %d, inten_curr: %d\n", xml_curr, mz_curr, inten_curr);

    // xml base case
    xml_dp->end_positions[xml_curr] = end;
    xml_curr++;
    xml_dp->total_spec = xml_curr;

    mz_dp->total_spec = df->source_total_spec;
    inten_dp->total_spec = df->source_total_spec;

    mz_dp->file_end = inten_dp->file_end = xml_dp->file_end = end;

    // Sanity check
    validate_positions(mz_dp->start_positions, mz_dp->total_spec);
    validate_positions(mz_dp->end_positions, mz_dp->total_spec);
    validate_positions(inten_dp->start_positions, inten_dp->total_spec);
    validate_positions(inten_dp->end_positions, inten_dp->total_spec);
    validate_positions(xml_dp->start_positions, xml_dp->total_spec);
    validate_positions(xml_dp->end_positions, xml_dp->total_spec);

    // Create division_t 

    division_t* div = (division_t*)malloc(sizeof(division_t));
    if(div == NULL)
        error("find_binary_quick: failed to allocate division_t.\n");

    div->xml = xml_dp;
    div->mz = mz_dp;
    div->inten = inten_dp;
    div->size = end; // Size is the end of the file

    return div;    
}



long
encodedLength_sum(data_positions_t* dp)
{
    if(dp == NULL)
        error("encodedLength_sum: NULL pointer passed in.\n");
    
    int i = 0;
    long res = 0;

    for(; i < dp->total_spec; i++)
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

        if (curr_div >= *divisions)
        {
            fprintf(stderr, "err: curr_div > divisions\ncurr_div: %d\ndivisions: %d\ncurr_div_i: %d\ncurr_size: %d\ni: %d\ntotal_spec: %d\n",
             curr_div, *divisions, curr_div_i, curr_size, i, dp->total_spec * 2);
            exit(-1);
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

data_positions_t***
new_get_binary_divisions(data_positions_t** ddp, int ddp_len, long* blocksize, int* divisions, long* threads)
{
    if(ddp == NULL)
        error("new_get_binary_divisions: NULL pointer passed in.\n");
    if(ddp_len < 1)
        error("new_get_binary_divisions: ddp_len < 1.\n");
    if(*threads < 1)
        error("new_get_binary_divisions: threads < 1.\n");

    data_positions_t*** r = malloc(sizeof(data_positions_t**) * ddp_len);

    if(r == NULL)
        error("new_get_binary_divisions: malloc failed.\n");

    int i = 0, j = 0,
        curr_div = 0,
        curr_div_i = 0,
        curr_size = 0;

    *divisions = *threads;
    
    for(j = 0; j < ddp_len; j++)
        r[j] = alloc_ddp(*divisions, (int)ceil(ddp[0]->total_spec/(*divisions)));

    long encoded_sum = encodedLength_sum(ddp[0]);
    if(encoded_sum <= 0)
        error("new_get_binary_divisions: encoded_sum <= 0.\n");

    long bs = encoded_sum/(*divisions);

    int bound = ddp[0]->total_spec;
    if(bound <= 0)
        error("new_get_binary_divisions: bound <= 0.\n");

    for(; i < bound; i++)
    {
        // if(curr_div_i >= r[0][curr_div]->total_spec)
        //     error("new_get_binary_divisions: curr_div_i >= r[0][curr_div]->total_spec.\n");
        if(curr_size >= bs)
        {
            curr_div++;
            curr_div_i = 0;
            curr_size = 0;
            if(curr_div > *divisions)
                break;
        }
        for(j = 0; j < ddp_len; j++)
        {
            r[j][curr_div]->start_positions[curr_div_i] = ddp[j]->start_positions[i];
            r[j][curr_div]->end_positions[curr_div_i] = ddp[j]->end_positions[i];
            r[j][curr_div]->total_spec++;
        }
        curr_size += (ddp[0]->end_positions[i] - ddp[0]->start_positions[i]);
        curr_div_i++;
    }

    curr_div--;

    /* add the remainder to the last division */
    for(; i < bound; i++)
    {
        for(j = 0; j < ddp_len; j++)
        {
            r[j][curr_div]->start_positions[curr_div_i] = ddp[j]->start_positions[i];
            r[j][curr_div]->end_positions[curr_div_i] = ddp[j]->end_positions[i];
            r[j][curr_div]->total_spec++;
        }
    }

    return r;
}

data_positions_t** 
new_get_xml_divisions(data_positions_t* dp, int divisions)
{
    data_positions_t** r;

    int i = 0,
        curr_div = 0,
        curr_div_i = 0,
        curr_size = 0;

    int bound = dp->total_spec,
        div_bound = (int)ceil(dp->total_spec/divisions);

    r = alloc_ddp(divisions, div_bound + divisions); // allocate extra room for remainders

    // check if r is null
    if(r == NULL)
    {
        fprintf(stderr, "err: r is null\n");
        exit(-1);
    }

    for(; i <= bound; i++)
    {
        if(curr_div_i > div_bound)
        {
            r[curr_div]->file_end = dp->file_end;
            curr_div++;
            curr_div_i = 0;
            curr_size = 0;
            if(curr_div == divisions)
                break;
        }

        r[curr_div]->start_positions[curr_div_i] = dp->start_positions[i];
        r[curr_div]->end_positions[curr_div_i] = dp->end_positions[i];
        r[curr_div]->total_spec++;
        curr_div_i++;
    }

    curr_div--;

    // put remainder in last division
    for(; i <= bound; i++)
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
write_dp(data_positions_t* dp, int fd)
{
    char* buff, *num_buff;


    // Write total_spec (spectrum count)
    num_buff = malloc(sizeof(int));
    *((int*)num_buff) = dp->total_spec;
    write_to_file(fd, num_buff, sizeof(int));

    // Write start positions
    buff = (char*)dp->start_positions;
    write_to_file(fd, buff, sizeof(off_t)*dp->total_spec);

    // Write end positions
    buff = (char*)dp->end_positions;
    write_to_file(fd, buff, sizeof(off_t)*dp->total_spec);

    return;
}

data_positions_t*
read_dp(void* input_map, long* position)
{
    data_positions_t* r = malloc(sizeof(data_positions_t));
    if(r == NULL) return NULL;

    // Read total_spec
    r->total_spec = *((int*)(input_map + *position));
    *position += sizeof(int);

    // Read start positions
    r->start_positions = (off_t*)(input_map + *position);
    *position += sizeof(off_t)*r->total_spec;

    // Read end positions
    r->end_positions = (off_t*)(input_map + *position);
    *position += sizeof(off_t)*r->total_spec;

    return r;
}

void
write_division(division_t* div, int fd)
{
    char* buff, *num_buff;

    // Write data_positions_t
    write_dp(div->xml, fd);
    write_dp(div->mz, fd);
    write_dp(div->inten, fd);

    // Write size of division
    num_buff = malloc(sizeof(size_t));
    *((size_t*)num_buff) = div->size;
    write_to_file(fd, num_buff, sizeof(size_t));

    return;
}

void
write_divisions(divisions_t* divisions, int fd)
{
    for(int i = 0; i < divisions->n_divisions; i++)
        write_division(divisions->divisions[i], fd);

    return;    
}

division_t*
read_division(void* input_map, long* position)
{
    division_t* r;

    r = malloc(sizeof(division_t));
    if(r == NULL) return NULL;

    r->xml = read_dp(input_map, position);
    r->mz = read_dp(input_map, position);
    r->inten = read_dp(input_map, position);
    r->size = *((size_t*)(input_map + *position));
    *position += sizeof(size_t);

    return r;
}

divisions_t*
read_divisions(void* input_map, long position, int n_divisions)
{
    divisions_t* r;

    r = malloc(sizeof(divisions_t));
    if(r == NULL) return NULL;
    r->divisions = malloc(sizeof(division_t*) * n_divisions);
    if(r->divisions == NULL) return NULL;

    for(int i = 0; i < n_divisions; i++)
        r->divisions[i] = read_division(input_map, &position);

    r->n_divisions = n_divisions;

    return r;
}


data_positions_t**
join_xml(divisions_t* divisions)
{
    data_positions_t** r;
    r = malloc(sizeof(data_positions_t*) * divisions->n_divisions);
    if(r == NULL) return NULL;
    for(int i = 0; i < divisions->n_divisions; i++)
        r[i] = divisions->divisions[i]->xml;
    return r;    
}

data_positions_t**
join_mz(divisions_t* divisions)
{
    data_positions_t** r;
    r = malloc(sizeof(data_positions_t*) * divisions->n_divisions);
    if(r == NULL) return NULL;
    for(int i = 0; i < divisions->n_divisions; i++)
        r[i] = divisions->divisions[i]->mz;
    return r;    
}

data_positions_t**
join_inten(divisions_t* divisions)
{
    data_positions_t** r;
    r = malloc(sizeof(data_positions_t*) * divisions->n_divisions);
    if(r == NULL) return NULL;
    for(int i = 0; i < divisions->n_divisions; i++)
        r[i] = divisions->divisions[i]->inten;
    return r;    
}

division_t**
create_divisions(division_t* div, long blocksize, long n_threads)
{
    divisions_t* r;

    r = malloc(sizeof(divisions_t));
    if(r == NULL) return NULL;

    r->divisions = malloc(sizeof(division_t*) * n_threads +1);
    if(r->divisions == NULL) return NULL;

    // r->n_divisions = n_threads;
    r->n_divisions = n_threads + 1; // n_divisions + 1 for the last division containing only remaining XML.

    //  Determine roughly how many spectra each division will contain
    long n_spec_per_div = div->mz->total_spec / n_threads;

    // Determine how many spectra will be left over
    long n_spec_leftover = div->mz->total_spec % n_threads;

    int spec_i = 0;
    int xml_i = 0;
    for(int i = 0; i < n_threads - 1; i++)
    {
        r->divisions[i] = malloc(sizeof(division_t));
        if(r->divisions[i] == NULL) return NULL;

        r->divisions[i]->xml = alloc_dp(n_spec_per_div*2);
        r->divisions[i]->xml->total_spec = 0;

        r->divisions[i]->mz = alloc_dp(n_spec_per_div);
        r->divisions[i]->mz->total_spec = 0;

        r->divisions[i]->inten = alloc_dp(n_spec_per_div);
        r->divisions[i]->inten->total_spec = 0;


        for(int j = 0; j < n_spec_per_div; j++)
        {
            // Copy MZ
            r->divisions[i]->mz->start_positions[j] = div->mz->start_positions[spec_i];
            r->divisions[i]->mz->end_positions[j] = div->mz->end_positions[spec_i];
            r->divisions[i]->mz->total_spec++;
            r->divisions[i]->size += div->mz->end_positions[spec_i] - div->mz->start_positions[spec_i];

            // Copy Inten
            r->divisions[i]->inten->start_positions[j] = div->inten->start_positions[spec_i];
            r->divisions[i]->inten->end_positions[j] = div->inten->end_positions[spec_i];
            r->divisions[i]->inten->total_spec++;
            r->divisions[i]->size += div->inten->end_positions[spec_i] - div->inten->start_positions[spec_i];

            spec_i++;
        }

        for(int j = 0; j < n_spec_per_div * 2; j++)
        {
            // Copy XML
            r->divisions[i]->xml->start_positions[j] = div->xml->start_positions[xml_i];
            r->divisions[i]->xml->end_positions[j] = div->xml->end_positions[xml_i];
            r->divisions[i]->xml->total_spec++;
            r->divisions[i]->size += div->xml->end_positions[xml_i] - div->xml->start_positions[xml_i];
            xml_i++;
        }
    }

    // End case: take the remaining spectra and put them in the last division
    // TODO: factor this out into a function
    int i = n_threads - 1;
    n_spec_per_div += n_spec_leftover;

    r->divisions[i] = malloc(sizeof(division_t));
    if(r->divisions[i] == NULL) return NULL;

    r->divisions[i]->xml = alloc_dp(n_spec_per_div*2);
    r->divisions[i]->xml->total_spec = 0;

    r->divisions[i]->mz = alloc_dp(n_spec_per_div);
    r->divisions[i]->mz->total_spec = 0;

    r->divisions[i]->inten = alloc_dp(n_spec_per_div);
    r->divisions[i]->inten->total_spec = 0;


    for(int j = 0; j < n_spec_per_div; j++)
    {
        // Copy MZ
        r->divisions[i]->mz->start_positions[j] = div->mz->start_positions[spec_i];
        r->divisions[i]->mz->end_positions[j] = div->mz->end_positions[spec_i];
        r->divisions[i]->mz->total_spec++;
        r->divisions[i]->size += div->mz->end_positions[spec_i] - div->mz->start_positions[spec_i];

        // Copy Inten
        r->divisions[i]->inten->start_positions[j] = div->inten->start_positions[spec_i];
        r->divisions[i]->inten->end_positions[j] = div->inten->end_positions[spec_i];
        r->divisions[i]->inten->total_spec++;
        r->divisions[i]->size += div->inten->end_positions[spec_i] - div->inten->start_positions[spec_i];

        spec_i++;
    }

    for(int j = 0; j < n_spec_per_div * 2; j++)
    {
        // Copy XML
        r->divisions[i]->xml->start_positions[j] = div->xml->start_positions[xml_i];
        r->divisions[i]->xml->end_positions[j] = div->xml->end_positions[xml_i];
        r->divisions[i]->xml->total_spec++;
        r->divisions[i]->size += div->xml->end_positions[xml_i] - div->xml->start_positions[xml_i];
        xml_i++;
    }

    // End case: remaining XML
    r->divisions[n_threads] = malloc(sizeof(division_t));
    if(r->divisions[n_threads] == NULL) return NULL;

    r->divisions[n_threads]->xml = alloc_dp(1);
    r->divisions[n_threads]->xml->start_positions[0] = div->xml->start_positions[xml_i];
    r->divisions[n_threads]->xml->end_positions[0] = div->xml->end_positions[xml_i];
    r->divisions[n_threads]->size += div->xml->end_positions[xml_i] - div->xml->start_positions[xml_i];

    r->divisions[n_threads]->mz = alloc_dp(0);
    r->divisions[n_threads]->inten = alloc_dp(0);

    return r;
}

int
preprocess_mzml(char* input_map,
                long  input_filesize,
                long* blocksize,
                long n_threads,
                data_format_t** df,
                divisions_t** divisions)
{
    struct timeval start, stop;

    gettimeofday(&start, NULL);

    print("\nPreprocessing...\n");

    *df = pattern_detect((char*)input_map);

    if (*df == NULL)
        return -1;

    division_t* div = find_binary_quick((char*)input_map, *df, input_filesize); // A division encapsulating the entire file

    if (div == NULL)
        return -1;

    *divisions = create_divisions(div, *blocksize, n_threads); // For now, create_divisions only returns one division

    if (*divisions == NULL)
        return -1;
    gettimeofday(&stop, NULL);

    print("Preprocessing time: %1.4fs\n", (stop.tv_sec-start.tv_sec)+((stop.tv_usec-start.tv_usec)/1e+6));  

}