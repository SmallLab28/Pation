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
typedef struct pt_scanner pt_scanner;

// func
typedef long (scan_num_fn) (pt_context *ctx, const char* content);
typedef long (scan_key_to_num_fn) (pt_context *ctx, const char * content, const char *key_word);

struct pt_scanner{
    const char *content; // content need to analysis
    size_t *start; // pointer start of content
    size_t *end; // pointer end of content
    long number; // number 
    const char *name; // name
    const char *capture_content_stream; // content in stream and endstream
    // func
    scan_num_fn *scan_num;
    scan_key_to_num_fn *scan_key_to_num;
};

typedef enum {
    PT_SYS_OK = 0,
    PT_SYS_IO,
    PT_SYS_MEM, 
    PT_SYS_PERMISSION,
}pt_sys;

typedef enum {
    PT_DOC_OK = 0,
    PT_DOC_ERR,
    PT_DOC_INVALID,
    PT_DOC_FUNC,
    PT_DOC_VAL,
    PT_DOC_FORMAT,
    PT_DOC_SYNTAX,
    PT_DOC_STREAM,
    PT_DOC_TABLE,
    PT_DOC_UNSUPPORTED, 
    PT_DOC_ENCRYPTED,
    PT_DOC_NOT_FOUND,
}pt_doc;


typedef enum {
    PT_SEEK_SET = SEEK_SET,
    PT_SEEK_CUR = SEEK_CUR,
    PT_SEEK_END = SEEK_END,
}pt_seek_mode;


typedef FILE *(pt_open_fn) (pt_context *ctx, pt_file *file); // fopen
typedef int (pt_seek_fn) (pt_context *ctx, FILE *f, int64 offset, pt_seek_mode mode);
typedef long (pt_tell_fn) (pt_context *ctx, FILE *f);
typedef size_t (pt_read_fn) (pt_context *ctx, char *buffer, uint64 size, size_t count, FILE *f);
typedef size_t (pt_u_read_fn) (pt_context *ctx, unsigned char *buffer, uint64 size, size_t count, FILE *f); //add read unsigned char func IO
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
    pt_u_read_fn *u_read;
    pt_gets_fn *gets;
    pt_close_fn *close;
};

// Constructor
pt_context *pt_ctx_create();
typedef void pt_ctx_free (pt_context *ctx);


struct pt_context {
    pt_sys sys;
    pt_doc doc;
    // func pointer ctx
    pt_ctx_free *free;
    int os_code;
    const char *message;

    // IO
    pt_seek_mode mode;
    pt_file *file;
    pt_scanner *scanner;
};



#ifdef __cplusplus

}
#endif
#endif

