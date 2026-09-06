#ifndef DOCUMENT_H
#define DOCUMENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdio.h>
#include<stdbool.h>
#include "pation/context.h"
  //struct pt_document
typedef struct pt_document pt_document;
  
  // func pointer
typedef void (close_fn) (pt_document *doc);
typedef long (size_fn) (pt_context *ctx, pt_document *doc);
typedef double (version_fn) (pt_context *ctx, pt_document *doc);
typedef bool (is_valid_pdf_fn) (pt_context *ctx, pt_document *doc);
  // Constructor
pt_document *pt_open_doc(pt_context *ctx, const char *file);


struct pt_document {
    FILE *f;
    const char *file_name;
    long size;
    double version_pdf;
    // func IO
    close_fn *close;

    // func checking pdf
    size_fn *check_size;
    version_fn *check_version;
    is_valid_pdf_fn *check_magic_byte;
};

#ifdef __cplusplus
}
#endif
#endif
