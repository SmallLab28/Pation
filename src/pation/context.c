#include "pation/context.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>


FILE *pt_open (pt_context *ctx, pt_file *file) {
    // for pdf
    file->method = "rb";
    file->f = fopen(file->file_name, file->method);
    if (file->f == NULL) {
        ctx->sys = PT_SYS_IO;
        printf("context.c: %d", __LINE__);
        return NULL;
    }
    return file->f;
}

int pt_seek(pt_context *ctx, FILE *f, int64 offset, pt_seek_mode mode){
    int n = fseek(f, offset, mode);
    if (n!=0) {
        ctx->sys = PT_SYS_IO;
        printf("context.c: %d", __LINE__);
        return 1;
    }
    return 0;
}


long pt_tell (pt_context *ctx, FILE *f){
    long n = ftell(f);
    if (n == -1L){
        ctx->sys = PT_SYS_IO;
        return -1L;
    }
    return n;
}

size_t pt_read (pt_context *ctx, char *buffer, uint64 size, size_t count, FILE *f){
    size_t n = fread(buffer, size, count, f);
    if (n == 0) {
        ctx->sys = PT_SYS_IO;
        printf("document.c:  %d", __LINE__);
        return 0;
    }
    return n;
}

size_t pt_u_read (pt_context *ctx, unsigned char *buffer, uint64 size, size_t count, FILE *f){
    size_t n = fread(buffer, size, count, f);
    if (n == 0){
        ctx->sys = PT_SYS_IO;
        printf("document.c: %d\n", __LINE__);
        return 0;
    }
    return n;
}

char *pt_gets (pt_context *ctx, char *buffer, size_t count, FILE *f){
    char *n = fgets(buffer, count, f);
    if (n == NULL){
        ctx->sys = PT_SYS_IO;
        return NULL;
    }
    return n;
}

int pt_close (pt_context *ctx, FILE *f){
    int n = fclose(f);
    if ( n == -1 ){
        ctx->sys = PT_SYS_IO;
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

long scan_number (pt_context *ctx, const char * content){
    char *cur;
    size_t len = strlen(content);
    int num = 0;
    for (int i = 0; i < len; i++){ 
        if (content[i] > 47 && content[i] < 58) {
            num = strtol(&content[i], &cur, 10);
            break;
        }
    }
    return num;
}

long scan_key_to_number (pt_context *ctx, const char *content, const char *key_word){
    size_t len = strlen(content);
    char *ptr = strstr(content, key_word);
    if (ptr==NULL) return ctx->doc = PT_DOC_NOT_FOUND;
    long num = 0;
    char *cur;
    for (int i = (ptr - content); i < len; i++){
        if(content[i] > 47 && content[i] < 58){
            num = strtol(&content[i], &cur, 10);
            return num;
        }
    }
    return ctx->doc = PT_DOC_NOT_FOUND;
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
    
    ctx->scanner = (pt_scanner*)malloc(sizeof(pt_scanner));
    if(ctx->scanner == NULL){
        free(ctx->file);
        free(ctx);
        return NULL;
    }

    ctx->scanner->scan_num = scan_number;
    ctx->scanner->scan_key_to_num = scan_key_to_number;
    // context
    ctx->file->open = pt_open;
    ctx->file->seek = pt_seek;
    ctx->file->tell = pt_tell;
    ctx->file->read = pt_read;
    ctx->file->u_read = pt_u_read;
    ctx->file->gets = pt_gets;
    ctx->file->close = pt_close;
    ctx->message = "success";
    ctx->doc = PT_DOC_OK;
    ctx->sys = PT_SYS_OK;
    ctx->os_code = 0;
    ctx->free = pt_ctx_free_fn;
    return ctx;
}

