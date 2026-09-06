#include<iostream>
#include "pation/document.h"
#include "pdf/xref.h"
#include "pation/context.h"
using namespace std;

int main () {
    pt_context *ctx = pt_ctx_create();
#ifdef _WIN32
    pt_document *doc = pt_open_doc(ctx,"C:\\Pation\\data\\a.pdf");
#elif __APPLE__
    pt_document *doc = pt_open_doc(ctx,"../data/test.pdf");
#else
    pt_document *doc = pt_open_doc(ctx,"../data/test.pdf");
#endif
    pdf_xref *xref = init_pdf_xref(ctx, doc);
    if (ctx == NULL || doc == NULL || xref == NULL){
        doc -> close(doc);
        xref -> close(xref);
        ctx -> free(ctx);
        return 0;
    }
    int a = xref->main(ctx,doc,xref) ? 1 : 0;
    if (a == 1) printf("%d\n", a);
    //int b = p
    doc -> close(doc);
    xref -> close(xref);
    ctx -> free(ctx);
    return 0;
}

