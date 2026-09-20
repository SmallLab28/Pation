#include "pdf/xref.h"
#include "pation/document.h"
#include "pation/state.h"
#include "pdf/parser.h"
#include "pdf/page.h"
#include <new>
#include <stdio.h>
#include <stdlib.h>

void drop_state (pt_state *st){
    // state 
    if (st == NULL) return;

    // doc
    if (st->doc != NULL){
        delete st->doc;
    }
    // xref
    if (st->xref != NULL){
        delete st->xref;
    }
    if (st->page != NULL){
        delete st->page;
    }
    if (st->parser != NULL) {
        delete st->parser;
    }
    free(st);
}


pt_state *pt_init_state(pt_context *ctx){
    pt_state *st = new (std::nothrow) pt_state{};
    if (st == NULL){
        return NULL;
    }
    // doc
    st->doc = new (std::nothrow) pt_document{};
    if(st->doc == NULL){
        drop_state(st);
        return NULL;
    }
    // xref
    st->xref  = new (std::nothrow) pdf_xref{};
    if (st->xref == NULL){
        drop_state(st);
        return NULL;
    }
    st->page = new (std::nothrow) pdf_page{};
    if (st->page == NULL){
        drop_state(st);
        return NULL;
    }
    st->parser = new (std::nothrow) pdf_parser{};
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
