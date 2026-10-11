
#include "pation/state.h"
#include "pation/context.h"
#include "pdf/parser.h"
#include "pdf/page.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include <unordered_map>

long parse_page_obj (pt_context *ctx, char *content_page){
    long page_obj = ctx->scanner->scan_key_to_num(ctx, content_page, "/Pages");
    if (page_obj <= 0 && ctx->doc == PT_DOC_NOT_FOUND) {
        std::cout << "parse_page_obj: missing /Pages token\n";
        return 0;
    }
    return page_obj;
}

std::vector<long> parse_kid_obj (pt_context *ctx, pt_state *st, char *content_kid){
    char *kids_pos = strstr(content_kid, "/Kids");
    if (!kids_pos) {
        std::cout << "parse_kid_obj: missing /Kids array\n";
        return {};
    }
    char *first_pos = strchr(kids_pos, '[');
    char *end_pos = strchr(kids_pos, ']');
    if (!first_pos || !end_pos || end_pos < first_pos) {
        std::cout << "parse_kid_obj: missing /Kids array\n";
        return {};
    }
    
    std::vector<long> dict_kid;
    char *ptr = first_pos + 1;
    char *end;
    while (ptr < end_pos) {
        if (*ptr >= '0' && *ptr <= '9') {
            long kid_obj = strtol(ptr, &end, 10);
            dict_kid.push_back(kid_obj);
            ptr = end;
            
            // Skip the generation number and 'R' so we don't parse them as kids
            while (ptr < end_pos && *ptr == ' ') ptr++;
            if (ptr < end_pos && *ptr >= '0' && *ptr <= '9') {
                strtol(ptr, &end, 10); // skip gen
                ptr = end;
                while (ptr < end_pos && *ptr == ' ') ptr++;
                if (ptr < end_pos && *ptr == 'R') ptr++;
            }
            st->page->kid_qty++;
        } else {
            ptr++;
        }
    }
    return dict_kid;
}

void main_parser (pt_context *ctx, pt_state *st){
    st->parser->kid_obj = parse_kid_obj;
    st->parser->page_obj = parse_page_obj;
    st->parser->main_parse = main_parser;
}
