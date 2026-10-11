#include <emscripten/bind.h>
#include <emscripten/emscripten.h>
#include <iostream>
#include <string>

#include "pation/context.h"
#include "pation/state.h"
#include "pation/document.h"
#include "pdf/page.h"
#include "pdf/xref.h"
#include "pdf/parser.h"
#include "pdf/stream.h"
#include "pdf/writer.h"

using namespace emscripten;

static pt_state* pt_load_pdf(pt_context *ctx, const char *filepath) {
    ctx->file->file_name = filepath;
    
    pt_state *st = pt_init_state(ctx);
    if (!st) return NULL; 

    st->doc->check_magic_byte(ctx, st);
    if (st->xref) {
        st->xref->find(ctx, st);
        int e = st->xref->dictionary(ctx, st);
        if (e == PT_DOC_TABLE) {
            // XREF dictionary built successfully
        }
        else if (e == PT_DOC_STREAM){
            // XREF dictionary built successfully
        }
        st->xref->trailer(ctx, st);
    } 
    return st; 
}

bool compress_pdf(std::string input_path, std::string output_path, int target_dpi, int target_quality) {
    pt_context *ctx = pt_ctx_create();
    pt_state *st = pt_load_pdf(ctx, input_path.c_str());

    if (!st) {
        ctx->free(ctx);
        std::cerr << "Failed to load PDF state." << std::endl;
        return false;
    }

    if (st && st->page && (ctx->doc == PT_DOC_TABLE || ctx->doc == PT_DOC_STREAM)) {
        st->page->page_root(ctx, st);
    }
    
    int res = -1;
    if (st && st->writer) {
        st->writer->target_dpi = target_dpi;
        st->writer->target_quality = target_quality;
        res = st->writer->rewrite(ctx, st, output_path.c_str());
    }
    
    ctx->free(ctx);
    return (res == 0);
}

EMSCRIPTEN_BINDINGS(pation_module) {
    function("compress_pdf", &compress_pdf);
}
