
#include"pation/state.h"
#include<stdio.h>
#include<stdlib.h>
#include"pation/document.h"
#include<string.h>
#include<stdbool.h>
#include<math.h>

long check_size (pt_context *ctx, pt_state *st){ 
    if (st->doc->f == NULL ){
        return ctx->sys_err = PT_SYS_IO;
    }
    if (fseek(st->doc->f, 0, SEEK_END) != 0){
        return ctx->sys_err = PT_SYS_IO;
    }
    st->doc->size = ftell(st->doc->f);
    if (st->doc->size < 1024) return ctx->doc_err = PT_DOC_INVALID;
    fseek(st->doc -> f, 0, SEEK_SET);
    return st->doc->size;
}



double version_of_pdf (pt_context *ctx, pt_state *st){ 
    if (st->doc->f == NULL ){ 
       return ctx->sys_err = PT_SYS_IO;
    }
    if (fseek(st->doc->f, 0, SEEK_SET) != 0) return ctx->sys_err = PT_SYS_IO;
    char buffer[9];
    if (fgets(buffer, sizeof(buffer), st->doc->f) == NULL) return ctx->sys_err = PT_SYS_IO;
    // atof convert from char* to double
    st->doc->version_pdf = atof(buffer + 5);
    return st->doc->version_pdf;
}


bool is_valid_file(pt_context *ctx, pt_state *st) {
    if (fseek(st->doc->f, 0, SEEK_SET) != 0){
        ctx->sys_err = PT_SYS_IO;
        return false;
    }

    char buffer[16];
    if (fgets(buffer, sizeof(buffer), st->doc->f) == NULL) {
        ctx->sys_err = PT_SYS_IO;
        return false;
    }

    fseek(st->doc->f, 0, SEEK_SET);
    if (strncmp(buffer, "%PDF", 4) != 0) {
        return false;
    }

    return true;
}


void main_doc (pt_context *ctx, pt_state *st){
    st->doc->f = fopen(st->doc->file_name, "rb");
    if (st->doc->f == NULL){
        ctx->message = "document.c __LINE__";
    }
    st->doc ->version_pdf       = -999;
    st->doc -> file_name        = st->doc->file_name;
    st->doc -> size             = -1;
    st->doc -> check_size       = check_size;
    st->doc -> check_version    = version_of_pdf;
    st->doc -> check_magic_byte = is_valid_file; 
}
