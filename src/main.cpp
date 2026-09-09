#include<iostream>
#include "pation/document.h"
#include "pdf/xref.h"
#include "pation/context.h"
using namespace std;

int main () {
    pt_context *ctx = pt_ctx_create();
    pt_state *st = pt_init_state();
#ifdef __WIN32
    ctx->file->file_name = "C:\\pation\\data\\a.pdf";
#elif __APPLE__
    ctx->file->file_name = "../data/a.pdf";
#else
    ctx->file->file_name = "../data/a.pdf";
#endif
    if( ctx == NULL || st == NULL){
        ctx->message = "main.cpp __LINE__";
        st  ->free(st);
        ctx ->free(ctx);
    }
    main_doc(ctx, st);
    int a = st->xref->call(ctx, st);
    std::cout << "SUCCESS" << "\n";
    st  ->free(st);
    ctx ->free(ctx); 
    return 0;
}

