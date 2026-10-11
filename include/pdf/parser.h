#ifndef PARSER_H
#define PARSER_H

#include "pation/context.h"
#include "pation/document.h"
#include "pation/state.h"
#include "pation/state.h"
#include <vector>
typedef struct pdf_parser pdf_parser;
// list
//typedef struct kid_index kid_index;

typedef long (parse_page_fn) (pt_context *ctx, char *content_page);
typedef void (main_parser_fn) (pt_context *ctx, pt_state *st);
typedef std::vector<long> (parse_kids_fn) (pt_context *ctx, pt_state *st, char *content_kid);
void main_parser(pt_context *ctx, pt_state *st);

struct pdf_parser{
    parse_page_fn *page_obj;
    parse_kids_fn *kid_obj;
    main_parser_fn *main_parse;
};

#endif
