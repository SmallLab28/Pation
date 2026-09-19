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


int scan_obj (pt_context *ctx, pt_state *st){
    long root_offset = st->xref->look(ctx, st, st->xref->root_obj);
    ctx->file->seek(ctx, ctx->file->f, root_offset, PT_SEEK_SET);

    char buffer[100] = {0};
    ctx->file->read(ctx, buffer, 1, 100, ctx->file->f);

    long page_obj = st->parser->lookup_page(ctx, buffer);
    long page_offset = st->xref->look(ctx, st, page_obj);

    ctx->file->seek(ctx, ctx->file->f, page_offset, PT_SEEK_SET);

    long index_buffer_start = ftell(ctx->file->f);
    char target[1] = {0};
    auto meet = false;
    int i = 0;
    while (meet == false){
        size_t read = ctx->file->read(ctx, target, sizeof(char), 1, ctx->file->f);
        if(target[0] == '>') {
            meet = true;
            break;
        }
        i++;
    }
    long index_now = ftell(ctx->file->f);
    const int dynamic = index_now - index_buffer_start;
    ctx->file->seek(ctx, ctx->file->f, page_offset, PT_SEEK_SET);
    char real_buf[dynamic + 1] = "\0";
    ctx->file->read(ctx, real_buf, sizeof(char), dynamic, ctx->file->f);
    std::string real_page(real_buf, dynamic);
    std::regex page_pattern(R"(/Count\s+([0-9]+)\s+)");
    std::smatch match_page;
    int page = 0; 
    if (std::regex_search(real_page, match_page, page_pattern)){
        page = std::stoi(match_page[1]);    
    }
    printf("Page is: %d\n", page);
    int n = st->parser->lookup_kid(ctx, real_buf);
    printf("%d\n", n);
    return 1; // this return just for testing 
}

void main_page (pt_context *ctx, pt_state *st){
    st->page->scan = scan_obj;
    st->page->main = main_page;
}
