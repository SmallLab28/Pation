#ifndef PAGE_H
#define PAGE_H

#include "pation/state.h"
#include "pdf/xref.h"
#include "pation/context.h"
#include "pation/document.h"
#include "pdf/xref.h"
#include <stdint.h>
#include <stdbool.h>
#include <vector>
//struct
typedef struct pdf_page pdf_page;


// main
typedef void (main_page_fn) (pt_context *ctx, pt_state *st);
void main_page(pt_context *ctx, pt_state *st);

// func
typedef int (page_root_fn) (pt_context *ctx, pt_state *st);


struct pdf_page{
    size_t page_num;
    size_t kid_qty;
    //parse page
    std::vector<long> kid;
    page_root_fn *page_root;
    main_page_fn *main;
};


#endif 
