#include <iostream>
#include "pdf/xref.h"
#include "pation/document.h"
#include "pdf/page.h"
#include "pdf/xref.h"
#include "pdf/parser.h"
#include "pation/context.h"
#include "pdf/stream.h"
#include "pdf/writer.h"
using namespace std;

pt_state* pt_load_pdf(pt_context *ctx, const char *filepath) {
    ctx->file->file_name = filepath;
    
    pt_state *st = pt_init_state(ctx);
    if (!st) return NULL; 

    st->doc->check_magic_byte(ctx, st);
    if (st->xref) {
        st->xref->find(ctx, st);
        int e = st->xref->dictionary(ctx, st);
        if (e == PT_DOC_TABLE) {
            printf("XREF dictionary built successfully!\n");
        }
        else if (e == PT_DOC_STREAM){
            printf("XREF dictionary built successfully\n");
        }
        else printf("XREF dictionary failed!\n");
        st->xref->trailer(ctx, st);
    } 
    return st; 
} 


int main (int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: pation <input.pdf> <output.pdf> [target_dpi (default 150)]\n";
        return 1;
    }

    const char *filepath = argv[1];
    const char *outpath = argv[2];
    int target_dpi = 150;
    int target_quality = 25;
    bool traditional_xref = false;

    if (argc >= 4) {
        target_dpi = std::atoi(argv[3]);
        if (target_dpi <= 0) target_dpi = 150;
    }
    if (argc >= 5) {
        target_quality = std::atoi(argv[4]);
        if (target_quality <= 0) target_quality = 25;
    }
    if (argc >= 6) {
        std::string arg = argv[5];
        if (arg == "1" || arg == "true" || arg == "traditional") {
            traditional_xref = true;
        }
    }

    pt_context *ctx = pt_ctx_create();
    pt_state *st = pt_load_pdf(ctx, filepath);

    if (!st) {
        std::cout << "Failed to load PDF state.\n";
        ctx->free(ctx);
        return 1;
    }

    if (st && st->page && (ctx->doc == PT_DOC_TABLE || ctx->doc == PT_DOC_STREAM)) {
        st->page->page_root(ctx, st);
    }
    
    if (st && st->writer) {
        st->writer->target_dpi = target_dpi;
        st->writer->target_quality = target_quality;
        st->writer->traditional_xref = traditional_xref;
        printf("Optimising and rewriting PDF to %s with %d DPI, %d Quality...\n", outpath, target_dpi, target_quality);
        int res = st->writer->rewrite(ctx, st, outpath);
        if (res == 0) {
            printf("PDF rewritten successfully! Check %s\n", outpath);
        } else {
            printf("Failed to rewrite PDF.\n");
        }
    }

    if (st) st->free(st);
    ctx->free(ctx); 
    return 0;
}
