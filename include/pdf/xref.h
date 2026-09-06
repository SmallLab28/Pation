#ifndef XREF_H
#define XREF_H

#ifdef __cplusplus
extern "C"{
#endif

#include <stdio.h>
#include <stdbool.h>
#include "pation/document.h"
#include "pation/context.h"
// Struct of page
typedef struct pdf_xref pdf_xref;


// Constuctor 
pdf_xref *init_pdf_xref(pt_context *ctx, pt_document *doc);




// func
typedef long (find_xref_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);
typedef size_t (count_page_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);
typedef void (close_xref_fn) (pdf_xref *xref);
typedef bool (is_valid_xref_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);
typedef int (dictionary_xref_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);
typedef int (parse_trailer_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);
typedef int (main_pation_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);
typedef long (lookup_offset_fn) (pt_context *ctx, pdf_xref *xref, int target_obj);
typedef int (jump_to_root_object_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);

// dictionary
typedef struct {
  int obj_id;
  long byte_offset;
  int gen_num;
  char status_obj;
}dictionary_xref_lookup; // --> lookup pointer


struct pdf_xref{
    long start_xref; 
    long ptr_end_xref; // -> end of xref table
    int root_obj; // -> byte offset of root object
    int root_gen_num; // -> byte offset of root object

    int base_obj, total_entries; // -> base obj and entry 
    long xref_data_offset; // xref table exclude obj && ent

    //vtable func
    close_xref_fn *close;
    find_xref_fn *find;
    is_valid_xref_fn *is_valid_xref;
    dictionary_xref_fn *dictionary; 
    dictionary_xref_lookup *lookup;
    parse_trailer_fn *trailer;
    jump_to_root_object_fn *jump;
    main_pation_fn *main;

};

#ifdef __cplusplus
}
#endif
#endif
