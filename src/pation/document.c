
#include"pation/context.h"
#include"pation/state.h"
#include<stdio.h>
#include<stdlib.h>
#include"pation/document.h"
#include<string.h>
#include<stdbool.h>
#include<math.h>

long check_size (pt_context *ctx, pt_state *st){ 
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
    st->doc->size = ctx->file->tell(ctx, ctx->file->f);
    if (st->doc->size < 1024) return ctx->doc_err = PT_DOC_INVALID;
    ctx->file->seek(ctx, ctx->file-> f, 0, PT_SEEK_SET);
    return st->doc->size;
}



double version_of_pdf (pt_context *ctx, pt_state *st){ 
    if (ctx->file->f == NULL ){ 
       return ctx->sys_err = PT_SYS_IO;
    }
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_SET);
    char buffer[9];
    ctx->file->gets(ctx, buffer, 9, ctx->file->f);
    // atof convert from char* to double
    st->doc->version_pdf = atof(buffer + 5);
    return st->doc->version_pdf;
}


bool is_valid_file(pt_context *ctx, pt_state *st) {
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_SET);
    char buffer[16];

    ctx->file->gets(ctx, buffer, 16, ctx->file->f);

    ctx->file->seek(ctx, ctx->file->f, 0, SEEK_SET);
    if (strncmp(buffer, "%PDF", 4) != 0) {
        return false;
    }

    return true;
}


void main_doc (pt_context *ctx, pt_state *st){ 
    if (ctx->file && ctx->file->open) {
        ctx->file->open(ctx, ctx->file);
    }
    st->doc ->version_pdf       = -999;
    st->doc -> file_name        = ctx->file->file_name;
    st->doc -> size             = -1;
    st->doc -> check_size       = check_size;
    st->doc -> check_version    = version_of_pdf;
    st->doc -> check_magic_byte = is_valid_file; 
}
