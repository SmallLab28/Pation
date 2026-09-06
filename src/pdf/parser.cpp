
#include "pation/document.h"
#include "pation/context"
#include "pdf/page.h"
#include "xref.h"
#include <stdio.h>
#include <stdlib.h>



// just testing
int jump_root_object (pt_context *ctx, pt_document *doc, pdf_xref *xref){
    if (xref->root_obj == 0){
        return ctx->doc_err = PT_DOC_VAL;
    }
    int jump = lookup_offset(ctx, xref, xref->root_obj);
    if (fseek(doc->f, jump, SEEK_SET) != 0) return ctx->sys_err = PT_SYS_IO;
    char buffer[45];
    if(fread(buffer, 1, 45, doc->f) < 45) return ctx->sys_err = PT_SYS_IO;
    return 1;// testing
}


