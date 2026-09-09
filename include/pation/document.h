#ifndef DOCUMENT_H
#define DOCUMENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include "pation/state.h"
#include "pation/context.h"
#include<stdint.h>
#include<stdio.h>
#include<stdbool.h>

  //struct pt_document
typedef struct pt_document pt_document;
  
  // func pointer
typedef long (size_fn) (pt_context *ctx, pt_state *st);
typedef double (version_fn) (pt_context *ctx, pt_state *st);
typedef bool (is_valid_pdf_fn) (pt_context *ctx, pt_state *st);
typedef void (main_doc_fn) (pt_context *ctx, pt_state *st);
void main_doc (pt_context *ctx, pt_state *st);



struct pt_document {
    FILE *f;
    const char *file_name;
    long size;
    double version_pdf;
    // func checking pdf
    size_fn *check_size;
    version_fn *check_version;
    is_valid_pdf_fn *check_magic_byte;
};

#ifdef __cplusplus
}
#endif
#endif
