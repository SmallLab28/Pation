#ifndef XREF_H
#define XREF_H


#include <vector>
#include "pation/state.h"
#include "miniz/miniz.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "pation/document.h"
#include "pation/context.h"
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
// Struct of page
typedef struct pdf_xref pdf_xref;

// func
typedef long (find_xref_fn) (pt_context *ctx, pt_state *st);
typedef size_t (count_page_fn) (pt_context *ctx, pt_state *st);
typedef int (xref_table_fn) (pt_context *ctx, pt_state *st);
typedef int (old_or_new_xref_fn) (pt_context *ctx, pt_state *st);
typedef std::vector<unsigned char> (xref_stream_fn) (pt_context *ctx, pt_state *st);
typedef int (dictionary_xref_fn) (pt_context *ctx, pt_state *st);
typedef int (parse_trailer_fn) (pt_context *ctx, pt_state *st);
typedef long (lookup_offset_fn) (pt_context *ctx, pt_state *st, int target_obj);
typedef int (jump_to_root_object_fn) (pt_context *ctx, pt_state *st);
// debug
void main_xref(pt_state *st);


// main
typedef void (main_xref_fn) (pt_state *st);
void main_xref (pt_state *st);

// dictionary
typedef struct {
    int obj_id;
    long byte_offset;
    int gen_num;
    char status_obj;
    uint8 stream_type_f1;
}dictionary_xref_lookup; // --> lookup pointer


// local xref in stream type if type 1 = 2
typedef struct {
    uint64 byte_offset_stream;
}local_xref_stream;



struct pdf_xref{
    long start_xref; 
    long ptr_end_xref; // -> end of xref table
    int root_obj; // -> byte offset of root object
    int root_gen_num; // -> byte offset of root object
    
    // stream type
    uint8 w1; // 0 = free, 1 = in-use, 2 = comporessed
    uint8 w2; // offset, if type2 = obj ID
    uint8 w3; // generation or index if f1 = type2
    uint8 row_size; 
    uint8 stream_type;
    const unsigned char *source;
    mz_ulong source_len;
    int row_size_stream_xref;
    std::vector<local_xref_stream> container_id;
    //end stream type
    
    // xref info 
    int base_obj, total_entries; // -> base obj and entry      
    long xref_data_offset; // xref table exclude obj && ent         
    // end xref info 
    
    // list
    std::vector<dictionary_xref_lookup> xref_store; // dictionary to lookup
    std::vector<local_xref_stream> local_xref;

    // func
    find_xref_fn *find;
    xref_table_fn *xref_table;
    xref_stream_fn *xref_stream;
    dictionary_xref_fn *dictionary;
    lookup_offset_fn *look; // find offset;
    parse_trailer_fn *trailer;
    jump_to_root_object_fn *jump;
    main_xref_fn *main;

};


#endif
