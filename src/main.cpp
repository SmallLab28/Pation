#include <iostream>
#include "pdf/xref.h"
#include "pation/document.h"
#include "pdf/page.h"
#include "pdf/xref.h"
#include "pdf/parser.h"
#include "pation/context.h"
using namespace std;

pt_state* pt_load_pdf(pt_context *ctx, const char *filepath) {
    ctx->file->file_name = filepath;
    
    pt_state *st = pt_init_state(ctx);
    if (!st) return NULL;
    
    st->doc->check_magic_byte(ctx, st);
    if (st->xref) {
        st->xref->find(ctx, st);
        int e = st->xref->dictionary(ctx, st);
        if (e) printf("XREF dictionary built successfully!\n");
        else printf("XREF dictionary failed!\n");
        st->xref->trailer(ctx, st); 
    } 
    return st; 
}

int main () {
    pt_context *ctx = pt_ctx_create();
#ifdef __WIN32
    const char *filepath = "C:\\Pation\\data\\xobject.pdf";
#else
    const char *filepath = "../data/a.pdf";
#endif
    pt_state *st = pt_load_pdf(ctx, filepath);
    
    if (st && st->page) {
        st->page->page_root(ctx, st);
    }
    if (st) st->free(st);
    ctx->free(ctx); 
    return 0;
}
