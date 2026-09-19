#ifndef PARSER_H
#define PARSER_H

#ifdef __cplusplus
extern "C"{
#endif

#include "pation/context.h"
#include "pation/document.h"
#include "pation/state.h"

typedef struct pdf_parser pdf_parser;
// list
//typedef struct kid_index kid_index;

typedef long (lookup_page_fn) (pt_context *ctx, char *content_page);
typedef void (main_parser_fn) (pt_context *ctx, pt_state *st);
typedef int (lookup_kids_fn) (pt_context *ctx, char *content_kid);
void main_parser(pt_context *ctx, pt_state *st);


struct pdf_parser{

    lookup_page_fn *lookup_page;
    lookup_kids_fn *lookup_kid;
    main_parser_fn *main_parse;
    
};

#ifdef __cplusplus
}
#endif
#endif
