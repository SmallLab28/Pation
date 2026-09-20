#include "pation/document.h"
#include "pation/context.h"
#include "pdf/page.h"
#include "pdf/xref.h"
#include "pdf/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <regex>
#include <iostream>
typedef int64_t int64;
typedef uint64_t uint64;


int page_root(pt_context *ctx, pt_state *st){
    long root_offset = st->xref->look(ctx, st, st->xref->root_obj);
    ctx->file->seek(ctx, ctx->file->f, root_offset, PT_SEEK_SET);
    

    long f_buf = ctx->file->tell(ctx, ctx->file->f);
    char buffer_page[1] = {0};
    auto meet_page = false;
    while (meet_page == false){
        ctx->file->read(ctx, buffer_page, sizeof(char), 1, ctx->file->f);
        if (buffer_page[0] == '>'){
            meet_page = true;
            break;
        }
    }
    long end_buf = ctx->file->tell(ctx, ctx->file->f);
    const size_t dynamic_page = end_buf - f_buf;
    char buffer[dynamic_page] = {0};
    ctx->file->seek(ctx, ctx->file->f, root_offset, PT_SEEK_SET);
    ctx->file->read(ctx, buffer, sizeof(char), dynamic_page, ctx->file->f);
    long page_obj = st->parser->page_obj(ctx, buffer);
    long page_offset = st->xref->look(ctx, st, page_obj);

    ctx->file->seek(ctx, ctx->file->f, page_offset, PT_SEEK_SET);

    long index_buffer_start = ctx->file->tell(ctx, ctx->file->f);

    char target[1];
    auto meet = false;
    while (meet == false){
        size_t read = ctx->file->read(ctx, target, sizeof(char), 1, ctx->file->f);
        if(target[0] == '>') {
            meet = true;
            break;
        }
    }
    long index_now = ctx->file->tell(ctx, ctx->file->f);
    const int dynamic = index_now - index_buffer_start;
    ctx->file->seek(ctx, ctx->file->f, page_offset, PT_SEEK_SET);
    char real_buf[dynamic + 1] = "\0";
    ctx->file->read(ctx, real_buf, sizeof(char), dynamic, ctx->file->f);
    std::string real_page(real_buf, dynamic);
    std::regex page_pattern(R"(/Count\s+([0-9]+)\s*)");
    std::smatch match_page;
    if (std::regex_search(real_page, match_page, page_pattern)){
        st->page->page_num = (size_t) std::stoi(match_page[1]);
    }

    // dictionary kids object
    st->page->kid = st->parser->kid_obj(ctx, st, real_buf); 
    for(int i = 0; i < st->page->kid.size(); i++) std::cout << "Kids object: " << st->page->kid[i] << "\n";
    return st->page->page_num;
}


void main_page (pt_context *ctx, pt_state *st){
    st->page->page_root = page_root;
    st->page->main = main_page;
}
