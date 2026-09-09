#include "pation/context.h"
#include <stdio.h>
#include <stdlib.h>


FILE *pt_open (pt_context *ctx, pt_file *file) {
    // for pdf
    file->method = "rb";
    file->f = fopen(file->file_name, file->method);
    if (file->f == NULL) {
        ctx->sys_err = PT_SYS_IO;
        printf("context.c: %d", __LINE__);
        return NULL;
    }
    return file->f;
}

int pt_seek(pt_context *ctx, FILE *f, int64 offset, pt_seek_mode mode){
    int n = fseek(f, offset, mode);
    if (n!=0) {
        ctx->sys_err = PT_SYS_IO;
        printf("context.c: %d", __LINE__);
        return 1;
    }
    return 0;
}


long pt_tell (pt_context *ctx, FILE *f){
    long n = ftell(f);
    if (n == -1L){
        ctx->sys_err = PT_SYS_IO;
        return -1L;
    }
    return n;
}

size_t pt_read (pt_context *ctx, char *buffer, uint64 size, size_t count, FILE *f){
    size_t n = fread(buffer, size, count, f);
    if (n == 0) {
        ctx->sys_err = PT_SYS_IO;
        printf("document.c:  %d", __LINE__);
        return 0;
    }
    return n;
}

char *pt_gets (pt_context *ctx, char *buffer, size_t count, FILE *f){
    char *n = fgets(buffer, count, f);
    if (n == NULL){
        ctx->sys_err = PT_SYS_IO;
        return NULL;
    }
    return n;
}

int pt_close (pt_context *ctx, FILE *f){
    int n = fclose(f);
    if ( n == -1 ){
        ctx->sys_err = PT_SYS_IO;
        return -1;
    }
    return 0;
}

void pt_ctx_free_fn(pt_context *ctx){
    if (ctx != NULL) {
        if (ctx->file != NULL) free(ctx->file);
        free(ctx);
    }
}


pt_context *pt_ctx_create(void){
    pt_context *ctx = (pt_context*)malloc(sizeof(pt_context));
    if(ctx == NULL){
        return NULL;
    }
    ctx->file = (pt_file*)malloc(sizeof(pt_file));
    if(ctx->file == NULL){
        free(ctx);
        return NULL;
    }
    ctx->file->open = pt_open;
    ctx->file->seek = pt_seek;
    ctx->file->tell = pt_tell;
    ctx->file->read = pt_read;
    ctx->file->gets = pt_gets;
    ctx->file->close = pt_close;
    ctx->message = "success";
    ctx->doc_err = PT_DOC_OK;
    ctx->sys_err = PT_SYS_OK;
    ctx->os_code = 0;
    ctx->free = pt_ctx_free_fn;
    return ctx;
}

