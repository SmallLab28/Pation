/*
    SMALLAB28 SOFTWARE
    ALLRIGHTS RESERVED
*/

#include "pation/context.h"
#include "pdf/xref.h"
#include "pation/document.h"
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <unordered_map>
#include <regex>


// to do need to improve the algorithm  
long find_xref_table (pt_context *ctx, pt_document *doc, pdf_xref *xref){
    if (doc->f == NULL ){
        return ctx->sys_err = PT_SYS_IO;
    }
    
    // Just working if the xref type like old xref
    if(fseek(doc->f, -1024, SEEK_END) != 0) return ctx->sys_err = PT_SYS_IO;
    // old xref table
    char buffer[1024];
    size_t size_buffer = sizeof(buffer);
    if (fread(buffer, 1, sizeof(buffer), doc->f) < size_buffer) return ctx->sys_err = PT_SYS_IO;
    std::string raw_byte(buffer, sizeof(buffer));
    size_t start_rb = raw_byte.rfind("startxref");
    size_t end_rb = raw_byte.rfind("%%EOF");
    std::string main_rb = raw_byte.substr(start_rb, end_rb - start_rb);
    std::regex start_xref_pattern(R"(([0-9]+)\s+)");
    std::smatch match;
    if (std::regex_search(main_rb, match, start_xref_pattern)){
        xref->start_xref = std::stoi(match[0]);
    }
    return xref->start_xref;
    // to do with new xref;
}


// not working if xref table type is stream type
bool valid_xref (pt_context *ctx, pt_document *doc, pdf_xref *xref){
    printf("[CHECK] Vao valid_xref tai dong %d\n", __LINE__);
    
    if (fseek(doc->f, 0 , SEEK_SET) != 0) return false;
    if (fseek(doc->f, xref->start_xref, SEEK_SET) != 0) return false;
    
    char buffer[64];
    if(fread(buffer, 1, 64, doc->f) < 64) return false;
    std::string xref_table(buffer, 64);
    std::cout << "xref_table val: " << xref_table << "\n";
    std::regex table_xref_pattern(R"(xref\s+([0-9]+)\s+([0-9]+)\s+)");
    std::smatch match;
    int object = 0, entry = 0;
    if (std::regex_search(xref_table, match, table_xref_pattern)){
        object = std::stoi(match[1]);
        entry = std::stoi(match[2]);
    }


    // try catch xref again fuck you microsoft 
    // this handler intended for bitch word to pdf
    if (entry == 0) {
        if (fseek(doc->f, 0 , SEEK_END) != 0) return false;
        if (fseek(doc->f, -2048, SEEK_END) != 0) return false;
        char retry_buffer[2048];
        if(fread(buffer, 1, 2048, doc->f) < 2048) return false;
        std::string retry_start_xref(retry_buffer,2048);
        size_t start_xref_retry = retry_start_xref.find("startxref");
        size_t end_xref_retry = retry_start_xref.find("%%EOF");
        std::string retry_main_xref = retry_start_xref.substr(start_xref_retry, end_xref_retry - start_xref_retry);
        std::cout << "buffer data is: " << retry_main_xref << "\n";
        std::regex main_retry_pattern(R"(startxref\s+([0-9]+)\s+)");
        std::smatch retry_match;
        if (std::regex_search(retry_main_xref, retry_match, main_retry_pattern)){
            xref->start_xref = std::stoi(retry_match[1]);
            // i know that stoi return value is int but its maybe ok
        }
        if(fseek(doc->f, xref->start_xref, SEEK_SET) != 0) return false;
        printf("xref now is: %ld\n", xref->start_xref);
        char retry_buf[15];
        if(fread(retry_buf, 1, 15, doc->f) < 15) return false;
        std::string catch_xref(retry_buf, 15);
        std::regex catch_xref_pattern(R"(xref\s+([0-9]+)\s+([0-9]+)\s+)");
        std::smatch catch_final;
        if(std::regex_search(catch_xref, catch_final, catch_xref_pattern)){
            object = std::stoi(catch_final[1]);
            entry = std::stoi(catch_final[2]);
        }
    }

    if (entry == 0){
        printf("PDF CORRUPT FILE\n");
        return false;
    }
    xref->xref_data_offset = ftell(doc->f);
    xref->base_obj = object; 
    xref->total_entries = entry; 
    return true;
}


// lookup xref
long lookup_offset (pt_context *ctx, pdf_xref *xref, int target_obj){
    int index = target_obj - xref->base_obj;
    if(index < 0 || index >= xref->total_entries){
        return ctx->doc_err = PT_DOC_FUNC;
    }
    return xref->lookup[index].byte_offset;
}



int dictionary_xref (pt_context *ctx, pt_document *doc, pdf_xref *xref) {
    if (valid_xref(ctx, doc, xref) == false) return ctx->doc_err = PT_DOC_FUNC;
    xref->lookup = (dictionary_xref_lookup*)malloc(xref->total_entries * sizeof(dictionary_xref_lookup));
    if(xref->lookup == NULL) return ctx->sys_err = PT_SYS_MEM;
    if(fseek(doc->f, 0, SEEK_SET) != 0) return ctx->sys_err = PT_SYS_IO;
    if(fseek(doc->f, xref->xref_data_offset, SEEK_SET) != 0) return ctx->sys_err = PT_SYS_IO;
    // old xref 
    for (int i = xref->base_obj; i < xref->total_entries; i++){
        long offset;
        int gen;
        char status;
        if (fscanf(doc->f, "%ld %d %c", &offset, &gen, &status) == 3){ 
            xref->lookup[i].obj_id = xref->base_obj + 1;
            xref->lookup[i].byte_offset = offset;
            xref->lookup[i].gen_num = gen;
            xref->lookup[i].status_obj = status;
            printf("Obj: %d | Offset: %ld | Status: %c\n", xref->base_obj + i, offset, status);
        }
        else {
            return ctx->sys_err = PT_SYS_IO;
        }
    }
    xref->ptr_end_xref = ftell(doc->f); 
    // new xref 
    return xref->ptr_end_xref;
}





int parse_trailer(pt_context *ctx, pt_document *doc, pdf_xref *xref){
    if(doc->f == NULL){
        return ctx->sys_err = PT_SYS_IO;
    } 
    if (fseek(doc->f, -1024, SEEK_END) != 0 ) return ctx->sys_err = PT_SYS_IO;
    char buffer[1024];
    if(fread(buffer, 1, 1024, doc->f) < 1024) return ctx->sys_err = PT_SYS_IO;
    char *raw_trailer = buffer;
    std::string trailer(raw_trailer, 1024);
    size_t start_find_root_trailer = trailer.rfind("<<");
    size_t end_find_root_trailer = trailer.rfind(">>");
    std::string main_root = trailer.substr(start_find_root_trailer, end_find_root_trailer - start_find_root_trailer);
    std::regex root_pattern (R"((/Root)\s+([0-9]+)\s+([0-9]+)\s+)");   
    /*
        how to use regex
        () is 1 group 
        [0-9] match number
        [a-z] match a-z
        [A-Z] match A-Z
        for example:
        (R"((/Root\s+)([0-9]+\s+)([0-9]+\s+))");
        use \s+ out of () to exclude the space
    */


    std::smatch match;
    int obj_num = 0, gen_num = 0;

    if(std::regex_search(main_root, match, root_pattern)){
        obj_num = std::stoi(match[2].str());
        gen_num = std::stoi(match[3].str()); // im stupid right here sorry guys
    }
    // std::stoi convert from string to int
    // std::stol convert from string to long
    xref->root_obj = obj_num;
    xref->root_gen_num = gen_num;
    return xref->root_obj;
}

// just testing
int jump_root_object (pt_context *ctx, pt_document *doc, pdf_xref *xref){
    if (xref->root_obj == 0){
        return ctx->doc_err = PT_DOC_VAL;
    }
    int jump = lookup_offset(ctx, xref, xref->root_obj);
    if (fseek(doc->f, jump, SEEK_SET) != 0) return ctx->sys_err = PT_SYS_IO;
    char buffer[45];
    if(fread(buffer, 1, 45, doc->f) < 45) return ctx->sys_err = PT_SYS_IO;
    return 1;
}


int main_function (pt_context *ctx, pt_document *doc, pdf_xref *xref){
    xref->find(ctx, doc, xref);
    int e = xref->dictionary(ctx, doc, xref);
    if (e) printf("Nap tu dien xref thanh cong!\n");
    
    // Cuối cùng mới bóc Trailer và nhảy
    int d = xref->trailer(ctx, doc, xref); 
    int f = xref->jump(ctx, doc, xref); 
        
    return 1;
}

// close constructor
void close_xref(pdf_xref *xref){
    if (xref != NULL) {
        if (xref->lookup != NULL){
            free(xref->lookup);
         }
         free(xref);
    }
}

// constuctor
pdf_xref *init_pdf_xref(pt_context *ctx, pt_document *doc){
    if ( doc == NULL ) {
        ctx->sys_err = PT_SYS_IO;
        ctx->message = "ERR with pt_document constructor maybe wrong path or not enough ram\n";
        return NULL;
    }
    pdf_xref *xref = (pdf_xref*)malloc(sizeof(*xref));
    if (xref == NULL) {
        ctx->sys_err = PT_SYS_MEM;
        ctx->message = "ERR with pdf_xref constructor \n";
        return NULL;
    }
    xref -> root_obj = 0;
    xref -> find = find_xref_table;
    xref -> close = close_xref;
    xref -> is_valid_xref = valid_xref;
    xref -> dictionary = dictionary_xref;
    xref -> trailer =  parse_trailer;
    xref -> jump = jump_root_object;
    xref -> main = main_function;
    return xref;
}

