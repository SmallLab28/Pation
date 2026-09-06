#ifndef PAGE_H
#define PAGE_H

#ifdef __cplusplus 
extern "C"{
#endif

#include "pation/context.h"
#include "pation/document.h"
#include "pdf/xref.h"
#include "stdint.h"
#include "stdbool.h"

//struct
typedef struct pdf_page pdf_page;


// Constuctor
pdf_page *page_init(pt_context *ctx, pt_document *doc);


typedef int (jump_fn) (pt_context *ctx, pt_document *doc, pdf_xref *xref);
typedef void (free_page_fn) (pdf_page);

struct pdf_page{
    int test;
    jump_fn *jump;
};


#ifdef __cplusplus
}
#endif
#endif 
