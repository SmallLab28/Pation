#include<stdio.h>
#include<stdlib.h>
#include"pation/document.h"
#include<string.h>
#include<stdbool.h>
#include<math.h>

long check_size (pt_context *ctx, pt_document *doc){ 
    if ( doc -> f == NULL ){
        return ctx->sys_err = PT_SYS_IO;
    }
    if (fseek(doc->f, 0, SEEK_END) != 0){
        return ctx->sys_err = PT_SYS_IO;
    }
    doc -> size = ftell(doc -> f);
    fseek(doc -> f, 0, SEEK_SET);
    return doc -> size;
}



double version_of_pdf (pt_context *ctx, pt_document *doc){ 
    if ( doc -> f == NULL ){ 
       return ctx->sys_err = PT_SYS_IO;
    }
    if (fseek(doc->f, 0, SEEK_SET) != 0) return ctx->sys_err = PT_SYS_IO;
    char buffer[9];
    if (fgets(buffer, sizeof(buffer), doc->f) == NULL) return ctx->sys_err = PT_SYS_IO;
    // atof convert from char* to double
    doc->version_pdf = atof(buffer + 5);
    return doc->version_pdf;
}


bool is_valid_file(pt_context *ctx, pt_document *doc) {
    if (doc == NULL || doc->f == NULL) {
        ctx->sys_err = PT_SYS_IO;
        return false;
    }

    if (fseek(doc->f, 0, SEEK_SET) != 0) {
        ctx->sys_err = PT_SYS_IO;
        return false;
    }

    char buffer[16];
    if (fgets(buffer, sizeof(buffer), doc->f) == NULL) {
        ctx->sys_err = PT_SYS_IO;
        return false;
    }

    fseek(doc->f, 0, SEEK_SET);
    if (strncmp(buffer, "%PDF", 4) != 0) {
        return false;
    }

    return true;
}


void close_doc(pt_document *doc){
    if ( doc != NULL ){
        if ( doc -> f != NULL ) {
            fclose(doc->f);
        }
    }
    free(doc);
}


pt_document *pt_open_doc(pt_context *ctx, const char *file){
    pt_document *doc = (pt_document*)malloc(sizeof(*doc));
    if ( doc == NULL ){
        ctx->sys_err = PT_SYS_MEM; 
        return NULL;
    }
    doc -> f = fopen(file, "rb");
    if ( doc -> f == NULL ){
        free(doc);
        ctx->sys_err = PT_SYS_IO;
        return NULL;
    }
    doc->version_pdf = -999;
    doc -> file_name = file;
    doc -> size = -1;
    doc -> close = close_doc;
    doc -> check_size = check_size;
    doc -> check_version = version_of_pdf;
    doc -> check_magic_byte = is_valid_file; 
    return doc;
}
