/*
    SMALLAB28 SOFTWARE
    ALLRIGHTS RESERVED
*/

#include "pation/state.h"
#include "pation/context.h"
#include "pdf/xref.h"
#include "miniz/miniz.h"
#include "pation/document.h"
#include <stdio.h>
#include "pdf/stream.h"
#include <vector>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <stdint.h>
#include <iostream>
#include <iostream>
#include <unordered_map>
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;




int old_or_new_xref(pt_context *ctx, pt_state *st){
    ctx->file->seek(ctx, ctx->file->f, -1024, PT_SEEK_END);
    char check_xref[1024] = {0};
    ctx->file->read(ctx, check_xref, 1, 1024, ctx->file->f);
    std::string xref(check_xref, 1024);
    size_t start_pos = xref.rfind("startxref");
    if (start_pos == std::string::npos) return ctx->doc = PT_DOC_INVALID;
    std::string real_xref = xref.substr(start_pos + 9);
    long byte_offset_start_xref = std::stol(real_xref);
    ctx->file->seek(ctx, ctx->file->f, byte_offset_start_xref, PT_SEEK_SET);
    char is_new_xref[100] = {0};
    ctx->file->read(ctx, is_new_xref, 1, 100, ctx->file->f);
    if (strstr(is_new_xref, "xref")){
        return ctx->doc = PT_DOC_TABLE;
    }
    return ctx->doc = PT_DOC_STREAM; // true is old false is new xref
}

// to do need to improve the algorithm (done)
// remainning stream type
long find_xref_table (pt_context *ctx, pt_state *st){
    ctx->file->seek(ctx, ctx->file->f, -1024, PT_SEEK_END);
    char buffer[1025] = {0};
    ctx->file->read(ctx, buffer, 1, 1024, ctx->file->f);
    
    std::string s(buffer, 1024);
    size_t p = s.rfind("startxref");
    if (p != std::string::npos) {
        try {
            st->xref->start_xref = std::stol(s.substr(p + 9));
        } catch (...) {
            st->xref->start_xref = 0;
        }
    } else {
        st->xref->start_xref = 0;
    }
    
    return st->xref->start_xref;
}


int parse_xref_table (pt_context *ctx, pt_state *st){
    if(old_or_new_xref(ctx, st) != PT_DOC_TABLE) return ctx->doc = PT_DOC_INVALID;
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_SET);
    ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
    long size = ctx->file->tell(ctx, ctx->file->f);
    ctx->file->seek(ctx, ctx->file->f, st->xref->start_xref, PT_SEEK_SET); 

    char buffer[65] = {0};
    ctx->file->read(ctx, buffer, 1, 64, ctx->file->f);
    int object = 0, entry = 0;
    
    char *xref_ptr = strstr(buffer, "xref");
    if (xref_ptr) {
        char *end;
        while (*xref_ptr && !isdigit(*xref_ptr)) xref_ptr++;
        object = strtol(xref_ptr, &end, 10);
        xref_ptr = end;
        while (*xref_ptr && !isdigit(*xref_ptr)) xref_ptr++;
        entry = strtol(xref_ptr, &end, 10);
        
        long exact_offset = st->xref->start_xref + (end - buffer);
        ctx->file->seek(ctx, ctx->file->f, exact_offset, PT_SEEK_SET);
    }
    

    // try catch xref again 
    if (entry == 0) {
        ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
        ctx->file->seek(ctx, ctx->file->f, -1024, PT_SEEK_END);
        char retry_buffer[1024] = {0};
        long byte_read = ctx->file->read(ctx, retry_buffer, 1, 1024, ctx->file->f);
        st->xref->start_xref = ctx->scanner->scan_key_to_num(ctx, retry_buffer, "startxref");
        
        if (st->xref->start_xref > 0) {
            ctx->file->seek(ctx, ctx->file->f, st->xref->start_xref, PT_SEEK_SET);
            char retry_buf[16] = {0};
            ctx->file->read(ctx, retry_buf, 1, 15, ctx->file->f);
            
            char *cxref = strstr(retry_buf, "xref");
            if(cxref){
                char *end;
                while (*cxref && !isdigit(*cxref)) cxref++;
                object = strtol(cxref, &end, 10);
                cxref = end;
                while (*cxref && !isdigit(*cxref)) cxref++;
                entry = strtol(cxref, &end, 10);
                
                long ptr_now = st->xref->start_xref + (end - retry_buf);
                ctx->file->seek(ctx, ctx->file->f, ptr_now, PT_SEEK_SET);
            }
        } else {
            return ctx->sys = PT_SYS_IO;
        }
    }

    if (entry == 0){
        printf("PDF CORRUPT FILE (xref.cpp): %d\n", __LINE__);
        return ctx->doc = PT_DOC_ERR;
    } 
    st->xref->xref_data_offset = ctx->file->tell(ctx, ctx->file->f);
    st->xref->base_obj = object; 
    st->xref->total_entries = entry; 
    return ctx->doc = PT_DOC_OK;
}

std::vector<unsigned char> parse_xref_stream(pt_context *ctx, pt_state *st){
    ctx->file->seek(ctx, ctx->file->f, st->xref->start_xref, PT_SEEK_SET);
    std::vector<char> buffer(64 * 1024, 0);
    size_t buffer_size = ctx->file->read(ctx, buffer.data(), 1, buffer.size() - 1, ctx->file->f);
    buffer[buffer_size] = 0;
    
    printf("DEBUG XREF BUFFER:\n%.200s\n", buffer);
    
    std::string need_buf(buffer.data(), buffer_size);
    size_t start_pos_arrow = need_buf.find("<<");
    size_t end_pos_arrow = need_buf.find(">>");

    if (start_pos_arrow == std::string::npos || end_pos_arrow == std::string::npos || start_pos_arrow >= end_pos_arrow) {
        return std::vector<unsigned char>();
    }

    std::string new_buf = need_buf.substr(start_pos_arrow, end_pos_arrow - start_pos_arrow);

    char *idx_ptr = strstr(buffer.data(), "/Index");
    if (idx_ptr) {
        char *end;
        while (*idx_ptr && !isdigit(*idx_ptr)) idx_ptr++;
        st->xref->base_obj = strtol(idx_ptr, &end, 10);
        idx_ptr = end;
        while (*idx_ptr && !isdigit(*idx_ptr)) idx_ptr++;
        st->xref->total_entries = strtol(idx_ptr, &end, 10);
    } else {
        // No /Index means this xref covers all objects starting from 0
        st->xref->base_obj = 0;
        st->xref->total_entries = ctx->scanner->scan_key_to_num(ctx, buffer.data(), "/Size");
    }

    
    long length_num = ctx->scanner->scan_key_to_num(ctx, buffer.data(), "/Length");
    if (length_num <= 0) return std::vector<unsigned char>();

    char *w_ptr = strstr(buffer.data(), "/W");
    if (w_ptr) {
        char *end;
        while (*w_ptr && !isdigit(*w_ptr)) w_ptr++;
        st->xref->w1 = (uint8)strtol(w_ptr, &end, 10);
        w_ptr = end;
        while (*w_ptr && !isdigit(*w_ptr)) w_ptr++;
        st->xref->w2 = (uint8)strtol(w_ptr, &end, 10);
        w_ptr = end;
        while (*w_ptr && !isdigit(*w_ptr)) w_ptr++;
        st->xref->w3 = (uint8)strtol(w_ptr, &end, 10);
    }

    
    st->xref->row_size = st->xref->w1 + st->xref->w2 + st->xref->w3;
    st->xref->row_size_stream_xref = st->xref->w1 + st->xref->w2 + st->xref->w3;
    // Improved algorithm: find "stream" keyword and handle CRLF properly
    size_t stream_pos = need_buf.find("stream");
    if (stream_pos != std::string::npos) {
        stream_pos += 6; // skip "stream"
        if (stream_pos < need_buf.size() && need_buf[stream_pos] == '\r') stream_pos++;
        if (stream_pos < need_buf.size() && need_buf[stream_pos] == '\n') stream_pos++;
        ctx->file->seek(ctx, ctx->file->f, st->xref->start_xref + stream_pos, PT_SEEK_SET);
    } else {
        return std::vector<unsigned char>();
    }
    
    std::vector<unsigned char> buffer_stream((size_t)length_num);
    size_t stream_size = ctx->file->u_read(ctx, buffer_stream.data(), 1, (size_t)length_num, ctx->file->f);
    if (stream_size != (size_t)length_num) return std::vector<unsigned char>();
    //raw data
    
    // Make sure dest_len is large enough for the decompressed xref stream
    st->stream->dest_len = st->xref->total_entries * 10; 
    
    std::vector<unsigned char> stream_xref_data = st->stream->uncompress(ctx, st, 
                                                    buffer_stream.data(), length_num);

    return stream_xref_data;
}

// lookup xref
long lookup_offset (pt_context *ctx, pt_state *st, int target_obj){
    int index = target_obj - st->xref->base_obj;
    if(index < 0 || index >= st->xref->total_entries){
        return ctx->doc = PT_DOC_FUNC;
    }
    if (st->xref->xref_store[index].byte_offset < 0 ){
        return ctx->doc = PT_DOC_STREAM;
    }
    return st->xref->xref_store[index].byte_offset;
}



#include <set>

int dictionary_xref (pt_context *ctx, pt_state *st) {
    long current_xref = st->xref->start_xref;
    int first_doc_type = -1;
    std::set<long> visited;

    while (current_xref > 0 && visited.find(current_xref) == visited.end()) {
        visited.insert(current_xref);
        st->xref->start_xref = current_xref;
        
        ctx->file->seek(ctx, ctx->file->f, current_xref, PT_SEEK_SET);
        char dict_buf[1025] = {0};
        ctx->file->read(ctx, dict_buf, 1, 1024, ctx->file->f);
        
        long prev_xref = ctx->scanner->scan_key_to_num(ctx, dict_buf, "/Prev");
        int doc_type = first_doc_type == -1 ? old_or_new_xref(ctx, st) : first_doc_type;
        if (first_doc_type == -1) first_doc_type = doc_type;
        
        if (doc_type == PT_DOC_TABLE) {
            st->xref->xref_table(ctx, st);
            ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_SET);
            ctx->file->seek(ctx, ctx->file->f, st->xref->xref_data_offset, PT_SEEK_SET);

            int new_size = st->xref->base_obj + st->xref->total_entries + 1;
            if (new_size > st->xref->xref_store.size()) {
                st->xref->xref_store.resize(new_size);
            }
            // old xref 
            for (int i = 0; i < st->xref->total_entries; i++){
                long offset;
                int gen;
                char status;
                if (fscanf(ctx->file->f, "%ld %d %c", &offset, &gen, &status) == 3){ 
                    int obj_id = st->xref->base_obj + i;
                    if (st->xref->xref_store[obj_id].byte_offset == 0 && obj_id != 0) {
                        st->xref->xref_store[obj_id].obj_id      = obj_id;
                        st->xref->xref_store[obj_id].byte_offset = offset;
                        st->xref->xref_store[obj_id].gen_num     = gen;
                        st->xref->xref_store[obj_id].status_obj  = status;
                    }
                }
                else {
                    return ctx->sys = PT_SYS_IO;
                }
            }
            st->xref->ptr_end_xref = ctx->file->tell(ctx, ctx->file->f); 
        }
        else if (doc_type == PT_DOC_STREAM) {
            long predictor = ctx->scanner->scan_key_to_num(ctx, dict_buf, "/Predictor");
            long columns = ctx->scanner->scan_key_to_num(ctx, dict_buf, "/Columns");

            std::vector<unsigned char> stream_xref_data = st->xref->xref_stream(ctx, st); 
            if (stream_xref_data.empty()) return ctx->doc = PT_DOC_ERR;
            
            // Decode PNG Predictor
            if (predictor >= 10 && columns > 0) {
                int row_bytes = columns + 1;
                std::vector<unsigned char> decoded;
                decoded.reserve((stream_xref_data.size() / row_bytes) * columns);
                
                std::vector<unsigned char> prev_row(columns, 0);
                for (size_t r = 0; r + row_bytes <= stream_xref_data.size(); r += row_bytes) {
                    int pred_type = stream_xref_data[r];
                    std::vector<unsigned char> cur_row(columns, 0);
                    for (int c = 0; c < columns; c++) {
                        unsigned char raw = stream_xref_data[r + 1 + c];
                        unsigned char up = prev_row[c];
                        unsigned char left = (c > 0) ? cur_row[c - 1] : 0;
                        if (pred_type == 1) cur_row[c] = raw + left;
                        else if (pred_type == 2) cur_row[c] = raw + up;
                        else cur_row[c] = raw + up;
                        decoded.push_back(cur_row[c]);
                    }
                    prev_row = cur_row;
                }
                stream_xref_data = decoded;
            }

            int w1 = st->xref->w1;
            int w2 = st->xref->w2;
            int w3 = st->xref->w3;
            int row_size = st->xref->row_size;

            std::vector<std::pair<int, int>> chunks;
            char *idx_ptr = strstr(dict_buf, "/Index");
            if (idx_ptr) {
                char *arr_start = strchr(idx_ptr, '[');
                if (arr_start) {
                    char *p = arr_start + 1;
                    while (*p && *p != ']') {
                        char *end;
                        int start_obj = strtol(p, &end, 10);
                        if (p == end) break;
                        p = end;
                        int count = strtol(p, &end, 10);
                        if (p == end) break;
                        p = end;
                        chunks.push_back({start_obj, count});
                    }
                }
            }
            if (chunks.empty()) {
                chunks.push_back({0, (int)ctx->scanner->scan_key_to_num(ctx, dict_buf, "/Size")});
            }

            int i = 0; 
            for (auto &chunk : chunks) {
                int new_size = chunk.first + chunk.second + 1;
                if (new_size > st->xref->xref_store.size()) st->xref->xref_store.resize(new_size);
                
                for (int cur_id = chunk.first; cur_id < chunk.first + chunk.second; cur_id++) {
                    if (i + row_size > stream_xref_data.size()) break;
                    
                    long type = 1; 
                    if (w1 > 0) {
                        type = 0;
                        for (int j = 0; j < w1; j++) type = (type << 8) | stream_xref_data[i + j];
                    }
                    long offset = 0;
                    if (w2 > 0) {
                        for (int j = 0; j < w2; j++) offset = (offset << 8) | stream_xref_data[i + w1 + j];
                    }
                    long gen = 0;
                    if (w3 > 0) {
                        for (int j = 0; j < w3; j++) gen = (gen << 8) | stream_xref_data[i + w1 + w2 + j];
                    }
                    
                    if (st->xref->xref_store[cur_id].byte_offset == 0 && cur_id != 0) {
                        st->xref->xref_store[cur_id].obj_id = cur_id;
                        st->xref->xref_store[cur_id].stream_type_f1 = type;
                        st->xref->xref_store[cur_id].byte_offset = offset;
                        st->xref->xref_store[cur_id].gen_num = gen;
                        st->xref->xref_store[cur_id].status_obj = (type == 0) ? 'f' : 'n';
                    }
                    i += row_size;
                }
            }
        }
        
        current_xref = prev_xref;
    }
    
    // Set max_id to the actual size of xref_store so writer knows the limit
    st->xref->total_entries = st->xref->xref_store.size();
    
    if (first_doc_type != -1) return ctx->doc = (pt_doc)first_doc_type;
    return ctx->doc = PT_DOC_UNSUPPORTED;
}


int parse_trailer(pt_context *ctx, pt_state *st){
    if(old_or_new_xref(ctx, st) == PT_DOC_TABLE){
        ctx->file->seek(ctx, ctx->file->f, 0, PT_SEEK_END);
        if (ctx->file->seek(ctx, ctx->file->f, -1024, PT_SEEK_END) != 0) return ctx->sys = PT_SYS_IO;
        char buffer[1025] = {0};
        size_t bytes_read = ctx->file->read(ctx, buffer, 1, 1024, ctx->file->f); 
        if (bytes_read == 0) return ctx->sys = PT_SYS_IO;

        std::string trailer(buffer, bytes_read);
        size_t start_find_root_trailer = trailer.rfind("<<");
        size_t end_find_root_trailer = trailer.rfind(">>");
        if(start_find_root_trailer == std::string::npos || end_find_root_trailer == std::string::npos || start_find_root_trailer >= end_find_root_trailer) {
            return ctx->doc = PT_DOC_FUNC;
        }
        std::string main_root = trailer.substr(start_find_root_trailer, end_find_root_trailer - start_find_root_trailer);
        int obj_num = 0, gen_num = 0;
        char *root_ptr = strstr(buffer, "/Root");
        if (root_ptr) {
            char *end;
            while (*root_ptr && !isdigit(*root_ptr)) root_ptr++;
            obj_num = strtol(root_ptr, &end, 10);
            root_ptr = end;
            while (*root_ptr && !isdigit(*root_ptr)) root_ptr++;
            gen_num = strtol(root_ptr, &end, 10);
        }

        st->xref->root_obj = obj_num; 
        st->xref->root_gen_num = gen_num;
        return ctx->doc = PT_DOC_TABLE;
    }
    else{
        return ctx->doc = PT_DOC_STREAM;
    }
} // this func is intended for parser

void main_xref (pt_state *st){
    st->xref->root_obj = 0;
    st->xref->find = find_xref_table;
    st->xref->xref_table = parse_xref_table;
    st->xref->xref_stream = parse_xref_stream;
    st->xref->dictionary = dictionary_xref;
    st->xref->trailer =  parse_trailer;
    st->xref->look = lookup_offset; 
    st->xref->main = main_xref;
}
