#ifndef CONTEXT_H
#define CONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;


typedef struct pt_context pt_context;
typedef struct pt_file pt_file;



typedef enum {
    PT_SYS_OK = 0,
    PT_SYS_IO,
    PT_SYS_MEM, 
    PT_SYS_PERMISSON,   
}pt_sys_err;

typedef enum {
    PT_DOC_OK = 0,
    PT_DOC_INVALID,
    PT_DOC_FUNC,
    PT_DOC_VAL,
    PT_DOC_FORMAT,
    PT_DOC_SYNTAX,
    PT_DOC_UNSUPPORTED, 
    PT_DOC_ENCRYPTED,
}pt_doc_err;


typedef enum {
    PT_SEEK_SET = SEEK_SET,
    PT_SEEK_CUR = SEEK_CUR,
    PT_SEEK_END = SEEK_END,
}pt_seek_mode;


typedef FILE *(pt_open_fn) (pt_context *ctx, pt_file *file); // fopen
typedef int (pt_seek_fn) (pt_context *ctx, FILE *f, int64 offset, pt_seek_mode mode);
typedef long (pt_tell_fn) (pt_context *ctx, FILE *f);
typedef size_t (pt_read_fn) (pt_context *ctx, char *buffer, uint64 size, size_t count, FILE *f);
typedef char *(pt_gets_fn) (pt_context *ctx, char *buffer, size_t count, FILE *f);
typedef int (pt_close_fn) (pt_context *ctx, FILE *f);
// IO context
struct pt_file{
    FILE *f;
    const char *file_name;
    const char *method; 
    // vtable func
    pt_open_fn *open;
    pt_seek_fn *seek;
    pt_tell_fn *tell;
    pt_read_fn *read;
    pt_gets_fn *gets;
    pt_close_fn *close;
};

// Constructor
pt_context *pt_ctx_create();
typedef void pt_ctx_free (pt_context *ctx);


struct pt_context {
    pt_sys_err sys_err;
    pt_doc_err doc_err;
    // func pointer ctx
    pt_ctx_free *free;
    int os_code;
    const char *message;

    // IO
    pt_seek_mode mode;
    pt_file *file;
};



#ifdef __cplusplus

}
#endif
#endif

