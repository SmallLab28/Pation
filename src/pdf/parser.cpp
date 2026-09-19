

#include "pation/state.h"
#include "pation/context.h"
#include "pdf/parser.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>

long lookup_page (pt_context *ctx, char *content_page){
    size_t quantity = strlen(content_page);
    std::string content_page_obj(content_page, quantity);
    size_t start_content = content_page_obj.find("<<"); size_t f_page_tok = content_page_obj.rfind("/Pages");
    std::string page_tok = content_page_obj.substr(f_page_tok, f_page_tok - start_content);
    std::regex page_tok_pt (R"((/Pages)\s+([0-9]+)\s+([0-9]+)\s+)");
    std::smatch match_tok;
    long page_obj = 0;
    if (std::regex_search(page_tok, match_tok, page_tok_pt)){
        page_obj = (long) std::stoi(match_tok[2]); // smatch always have match[0];
    }
    return page_obj;
}

int lookup_kid (pt_context *ctx, char *content_kid){
    int count = 0;
    size_t quantity = strlen(content_kid);
    std::string buffer_kid (content_kid, quantity);
    std::cout << "Buffer kid: " << buffer_kid << "\n";
    size_t start_pos_kids = buffer_kid.find("/Kids");
    size_t end_pos_kids = buffer_kid.rfind("]");
    std::string real_kid = buffer_kid.substr(start_pos_kids, end_pos_kids - start_pos_kids);
    std::regex kid_pattern(R"((\d+)\s+0\s+R)");
    std::smatch match_kid;
    long kid_obj = 0;
    if(std::regex_search(real_kid, match_kid, kid_pattern)){
        kid_obj = (long) std::stoi(match_kid[1]);
    }
    return kid_obj;// experiment
}

void main_parser (pt_context *ctx, pt_state *st){
    st->parser->lookup_kid = lookup_kid;
    st->parser->lookup_page = lookup_page;
    st->parser->main_parse = main_parser;
}
