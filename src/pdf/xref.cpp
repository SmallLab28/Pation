/*
    SMALLAB28 SOFTWARE
    ALLRIGHTS RESERVED
*/

#include "pation/state.h"
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
long find_xref_table (pt_context *ctx, pt_state *st){
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_SET); 
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END); 
    long size = ctx->file->tell(ctx, ctx->file->f);
    long read_size = (size < 1024) ? size : 1024;
    if (ctx->file->seek(ctx, ctx->file->f, -read_size, PT_SEEK_END) != 0) return ctx->sys_err = PT_SYS_IO;
    
    char buffer[1024];
    
    size_t bytes_read = ctx->file->read(ctx, buffer, 1, read_size, ctx->file->f); 
    if (bytes_read == 0) return ctx->sys_err = PT_SYS_IO;
    
    std::string raw_byte(buffer, bytes_read);
    size_t start_rb = raw_byte.rfind("startxref");
    size_t end_rb = raw_byte.rfind("%%EOF");
    
    if(start_rb == std::string::npos || end_rb == std::string::npos || start_rb >= end_rb) {
        return ctx->doc_err = PT_DOC_FUNC; 
    }
    
    std::string main_rb = raw_byte.substr(start_rb, end_rb - start_rb);
    std::regex start_xref_pattern(R"(([0-9]+)\s+)");
    std::smatch match;
    
    if (std::regex_search(main_rb, match, start_xref_pattern)){
        st->xref->start_xref = std::stoi(match[1].str()); 
    }
    return st->xref->start_xref;
}


// not working if xref table type is stream type
bool valid_xref (pt_context *ctx, pt_state *st){
    if(ctx->doc_err == PT_DOC_INVALID) return false;
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_SET);
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
    long size = ctx->file->tell(ctx, ctx->file->f);
    ctx->file->seek(ctx, ctx->file->f, st->xref->start_xref, PT_SEEK_SET); 

    char buffer[64];
    ctx->file->read(ctx, buffer, 1, 64, ctx->file->f);
    std::string xref_table(buffer, 64);
    std::regex table_xref_pattern(R"(xref\s+([0-9]+)\s+([0-9]+)\s+)");
    std::smatch match;
    int object = 0, entry = 0;
    if (std::regex_search(xref_table, match, table_xref_pattern)){
        object = std::stoi(match[1]);
        entry = std::stoi(match[2]);
        long match_1 = match.position(0);
        long match_2 = match.length(0);
        long exact_offset = st->xref->start_xref + match_1 + match_2;
        ctx->file->seek(ctx, ctx->file->f, exact_offset, PT_SEEK_SET);
    }
    

    // try catch xref again fuck you microsoft 
    // this handler intended for bitch word to pdf
    if (entry == 0) {
        ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
        long read_size = (size < 1024) ? size : 1024;
        ctx->file->seek(ctx, ctx->file->f, -read_size, PT_SEEK_END);
        char retry_buffer[1024] = {0};
        ctx->file->read(ctx, retry_buffer, 1, read_size, ctx->file->f);
        std::string retry_start_xref(retry_buffer, read_size);
        size_t start_xref_retry = retry_start_xref.find("startxref");
        size_t end_xref_retry = retry_start_xref.find("%%EOF", start_xref_retry);
        if (start_xref_retry == std::string::npos || end_xref_retry == std::string::npos || start_xref_retry >= end_xref_retry) {
            return false;
        }
        std::string retry_main_xref = retry_start_xref.substr(start_xref_retry, end_xref_retry - start_xref_retry); 
        std::regex main_retry_pattern(R"(startxref\s+([0-9]+)\s+)");
        std::smatch retry_match;
        if (std::regex_search(retry_main_xref, retry_match, main_retry_pattern)){
            st->xref->start_xref = (long) std::stoi(retry_match[1]);
            // i know that stoi return value is int but its maybe ok
        }
        ctx->file->seek(ctx, ctx->file->f, st->xref->start_xref, PT_SEEK_SET);
        char retry_buf[15];
        ctx->file->read(ctx, retry_buf, 1, 15, ctx->file->f);
        std::string catch_xref(retry_buf, 15);
        std::regex catch_xref_pattern(R"(xref\s+([0-9]+)\s+([0-9]+)\s+)");
        std::smatch catch_final;
        if(std::regex_search(catch_xref, catch_final, catch_xref_pattern)){
            object = std::stoi(catch_final[1]);
            entry = std::stoi(catch_final[2]);
            long match_start_pos = catch_final.position(0);
            long match_total_length = catch_final.length(0);
            long ptr_now = st->xref->start_xref + match_start_pos + match_total_length;
            ctx->file->seek(ctx, ctx->file->f, ptr_now, PT_SEEK_SET);
        }
    }

    if (entry == 0){
        printf("PDF CORRUPT FILE\n");
        return false;
    } 
    st->xref->xref_data_offset = ctx->file->tell(ctx, ctx->file->f);
    st->xref->base_obj = object; 
    st->xref->total_entries = entry; 
    return true;
}


// lookup xref
long lookup_offset (pt_context *ctx, pt_state *st, int target_obj){
    int index = target_obj - st->xref->base_obj;
    if(index < 0 || index >= st->xref->total_entries){
        return ctx->doc_err = PT_DOC_FUNC;
    }
    return st->xref->lookup[index].byte_offset;
}



int dictionary_xref (pt_context *ctx, pt_state *st) {
    if (valid_xref(ctx, st) == false) return ctx->doc_err = PT_DOC_FUNC;
    st->xref->lookup = (dictionary_xref_lookup*)malloc(st->xref->total_entries * sizeof(dictionary_xref_lookup));
    if(st->xref->lookup == NULL) return ctx->sys_err = PT_SYS_MEM;
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_SET);
    ctx->file->seek(ctx, ctx->file->f, st->xref->xref_data_offset, PT_SEEK_SET);
    // old xref 
    for (int i = 0; i < st->xref->total_entries; i++){
        long offset;
        int gen;
        char status;
        if (fscanf(ctx->file->f, "%ld %d %c", &offset, &gen, &status) == 3){ 
            st->xref->lookup[i].obj_id      = st->xref->base_obj + i;
            st->xref->lookup[i].byte_offset = offset;
            st->xref->lookup[i].gen_num     = gen;
            st->xref->lookup[i].status_obj  = status;
            printf("Obj: %d | Offset: %ld | Status: %c\n", st->xref->base_obj + i, offset, status);
        }
        else {
            return ctx->sys_err = PT_SYS_IO;
        }
    }
    st->xref->ptr_end_xref = ctx->file->tell(ctx, ctx->file->f); 
    // new xref 
    return st->xref->ptr_end_xref;
    // this func i'll improve later maybe is vector or arena~~~
}





int parse_trailer(pt_context *ctx, pt_state *st){ 
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
    long size = ctx->file->tell(ctx, ctx->file->f);
    long read_size = (size < 1024) ? size : 1024;
    if (ctx->file->seek(ctx, ctx->file->f, -read_size, PT_SEEK_END) != 0) return ctx->sys_err = PT_SYS_IO;
    char buffer[1024];
    size_t bytes_read = ctx->file->read(ctx, buffer, 1, read_size, ctx->file->f); 
    if (bytes_read == 0) return ctx->sys_err = PT_SYS_IO;
    
    std::string trailer(buffer, bytes_read);
    size_t start_find_root_trailer = trailer.rfind("<<");
    size_t end_find_root_trailer = trailer.rfind(">>");
    if(start_find_root_trailer == std::string::npos || end_find_root_trailer == std::string::npos || start_find_root_trailer >= end_find_root_trailer) {
        return ctx->doc_err = PT_DOC_FUNC;
    }
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
    st->xref->root_obj = obj_num;
    st->xref->root_gen_num = gen_num;
    return st->xref->root_obj;
 } // this func is intended for parser



// test local func
int caller (pt_context *ctx, pt_state *st){
    st->xref->find(ctx, st);
    
    int e = st->xref->dictionary(ctx, st);
    if (e) printf("XREF dictionary built successfully!\n");
    else printf("XREF dictionary failed!\n");

    int d = st->xref->trailer(ctx, st); 
        
    return 1;
}



void main_xref (pt_state *st){
    st->xref -> root_obj = 0;
    st->xref -> find = find_xref_table;
    st->xref -> is_valid_xref = valid_xref;
    st->xref -> dictionary = dictionary_xref;
    st->xref -> trailer =  parse_trailer;
    st->xref -> call = caller;
    st->xref -> main = main_xref;
}

