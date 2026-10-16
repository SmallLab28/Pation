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
    if (!st || !st->xref || st->xref->root_obj <= 0) return 0;
    long root_offset = st->xref->look(ctx, st, st->xref->root_obj);
    if (root_offset <= 0) return 0;

    std::vector<char> buffer_vec(8192, 0);
    if (ctx->file->seek(ctx, ctx->file->f, root_offset, PT_SEEK_SET) != 0) return 0;
    size_t n_read = ctx->file->read(ctx, buffer_vec.data(), sizeof(char), buffer_vec.size() - 1, ctx->file->f);
    if (n_read == 0) return 0;
    char *buffer = buffer_vec.data();

    long page_obj = st->parser->page_obj(ctx, buffer);
    if (page_obj <= 0) return 0;
    long page_offset = st->xref->look(ctx, st, page_obj);
    if (page_offset <= 0) return 0;

    std::vector<char> real_buf_vec(16384, 0);
    if (ctx->file->seek(ctx, ctx->file->f, page_offset, PT_SEEK_SET) != 0) return 0;
    size_t page_read = ctx->file->read(ctx, real_buf_vec.data(), sizeof(char), real_buf_vec.size() - 1, ctx->file->f);
    if (page_read == 0) return 0;
    char *real_buf = real_buf_vec.data();
    std::string real_page(real_buf, page_read);

    std::regex page_pattern(R"(/Count\s+([0-9]+))");
    std::smatch match_page;
    if (std::regex_search(real_page, match_page, page_pattern)){
        st->page->page_num = (size_t) strtol(match_page[1].str().c_str(), nullptr, 10);
    }

    st->page->kid = st->parser->kid_obj(ctx, st, real_buf); 
    
    return st->page->page_num;
}


void main_page (pt_context *ctx, pt_state *st){
    st->page->page_root = page_root;
    st->page->main = main_page;
}
