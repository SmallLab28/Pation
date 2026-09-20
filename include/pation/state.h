#ifndef STATE_H
#define STATE_H

#include "pation/context.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct pdf_page pdf_page;
typedef struct pt_state pt_state;
typedef struct pt_document pt_document;
typedef struct pdf_xref pdf_xref;
typedef struct pdf_parser pdf_parser;
// Constructor
pt_state *pt_init_state (pt_context *ctx);

typedef void (pt_drop_state) (pt_state *st);

struct pt_state {
    // struct function
    pt_document *doc;
    pdf_xref *xref;
    pdf_page *page;
    pdf_parser *parser;

    // state life time
    pt_drop_state *free;
};

#endif
