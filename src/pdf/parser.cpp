
#include "pation/state.h"
#include "pation/context.h"
#include "pdf/parser.h"
#include "pdf/page.h"
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>

long parse_page_obj (pt_context *ctx, char *content_page){
    size_t quantity = strlen(content_page);
    std::string content_page_obj(content_page, quantity);
    size_t start_content = content_page_obj.find("<<"); 
    size_t f_page_tok = content_page_obj.rfind("/Pages");
    std::string page_tok = content_page_obj.substr(f_page_tok, f_page_tok - start_content);
    std::regex page_tok_pt (R"((/Pages)\s+([0-9]+)\s+([0-9]+)\s+)");
    std::smatch match_tok;
    long page_obj = 0;
    if (std::regex_search(page_tok, match_tok, page_tok_pt)){
        page_obj = (long) std::stoi(match_tok[2]); // smatch always have match[0];
    }
    return page_obj;
}

std::vector<long> parse_kid_obj (pt_context *ctx, pt_state *st, char *content_kid){
    size_t quantity = strlen(content_kid);
    std::string buffer_kid (content_kid, quantity);
    std::cout << "BUFFER KID: " << buffer_kid << "\n";
    size_t first_pos = buffer_kid.find("[", buffer_kid.find("/Kids"));
    size_t end_pos = buffer_kid.find("]", buffer_kid.find("/Kids"));
    std::string real_kid = buffer_kid.substr(first_pos, end_pos - first_pos);
    std::cout << "REAL KID: " << real_kid << "\n";
    const char *buffer = real_kid.c_str();
    const char *ptr = buffer;
    char *end;
    long kid_obj = 0;
    std::vector<long> dict_kid;
    while (*ptr != '\0'){
        if(*ptr >= '1' && *ptr <= '9'){
            kid_obj = strtol(ptr, &end, 10);
            dict_kid.push_back(kid_obj);
            ptr = end;
            st->page->kid_qty++;
        }
        else ptr++; 
    }
    return dict_kid;
}

void main_parser (pt_context *ctx, pt_state *st){
    st->parser->kid_obj = parse_kid_obj;
    st->parser->page_obj = parse_page_obj;
    st->parser->main_parse = main_parser;
}
