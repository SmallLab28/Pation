#ifndef PAGE_H
#define PAGE_H

#ifdef __cplusplus 
extern "C"{
#endif
#include "pdf/xref.h"
#include "pation/context.h"
#include "pation/document.h"
#include "pdf/xref.h"
#include "stdint.h"
#include "stdbool.h"

//struct
typedef struct pdf_page pdf_page;


// main
typedef void (main_parse_fn) (pt_context *ctx, pt_state);

// func
typedef int (scan_obj_fn) (pt_context *ctx, pt_state *st);

struct pdf_page{
    int test;
    main_parse_fn *main;
};


#ifdef __cplusplus
}
#endif
#endif 
