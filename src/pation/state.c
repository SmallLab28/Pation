
#include "pdf/xref.h"
#include "pation/document.h"
#include "pation/state.h"
#include "pdf/parser.h"
#include "pdf/page.h"
#include <stdio.h>
#include <stdlib.h>

void drop_state (pt_state *st){
    // state 
    if (st == NULL) return;

    // doc
    if (st->doc != NULL){
        free(st->doc);
    }
    // xref
    if (st->xref != NULL){
        free(st->xref->lookup);
        free(st->xref);
    }
    if (st->page != NULL){
        free(st->page);
    }
    if (st->parser != NULL) {
        free(st->parser);
    }
    free(st);
}


pt_state *pt_init_state(pt_context *ctx){
    pt_state *st = (pt_state*)calloc(1, sizeof(*st));
    if (st == NULL){
        return NULL;
    }
    // doc
    st->doc = calloc(1, sizeof(*(st->doc)));
    if(st->doc == NULL){
        drop_state(st);
        return NULL;
    }
    // xref
    st->xref  = calloc(1, sizeof(*(st->xref)));
    if (st->xref == NULL){
        drop_state(st);
        return NULL;
    }
    st->page = calloc(1, sizeof(*(st->page)));
    if (st->page == NULL){
        drop_state(st);
        return NULL;
    }
    st->parser = calloc(1, sizeof(*(st->parser)));
    if (st->parser == NULL){
        drop_state(st);
        return NULL;
    }
    main_doc(ctx, st);
    main_xref(st);
    main_page(ctx, st);
    main_parser(ctx, st);
    st->free = drop_state;
    return st;
}
