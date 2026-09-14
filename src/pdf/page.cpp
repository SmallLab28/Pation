#include "pation/document.h"
#include "pation/context.h"
#include "pdf/page.h"
#include "pdf/xref.h"
#include "pdf/parser.h"
#include <stdio.h>
#include <stdlib.h>


int scan_obj (pt_context *ctx, pt_state *st){
    long root_offset = st->xref->look(ctx, st, st->xref->root_obj);
    ctx->file->seek(ctx, ctx->file->f, root_offset, PT_SEEK_SET);
    char buffer[100] = {0};
    ctx->file->read(ctx, buffer, 1, 100, ctx->file->f);
    long page_obj = st->parser->lookup_page_obj(ctx, buffer);
    long page_offset = st->xref->look(ctx, st, page_obj);
    ctx->file->seek(ctx, ctx->file->f, page_offset, PT_SEEK_SET);
    char buffer_2[200] = {0};
    ctx->file->read(ctx, buffer_2, 1, 200, ctx->file->f);
    printf("buffer page obj: %s\n", buffer_2);
    printf("buffer root obj: %s\n", buffer);
    return 1; // this return just for testing 
}

void main_page (pt_context *ctx, pt_state *st){
    st->page->scan = scan_obj;
    st->page->main = main_page;
}
