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

static void wasm_progress_thunk(int current, int total, double percent, void *user_data) {
    val *cb = (val*)user_data;
    if (cb && !cb->isUndefined() && !cb->isNull()) {
        try {
            (*cb)(current, total, percent);
        } catch (...) {
        }
    }
}

bool compress_pdf(std::string input_path, std::string output_path, int target_dpi, int target_quality, bool traditional_xref) {
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
        st->writer->traditional_xref = traditional_xref;
        st->writer->progress_cb = nullptr;
        st->writer->progress_user_data = nullptr;
        res = st->writer->rewrite(ctx, st, output_path.c_str());
    }
    
    ctx->free(ctx);
    return (res == 0);
}

bool compress_pdf_with_progress(std::string input_path, std::string output_path, int target_dpi, int target_quality, val on_progress, bool traditional_xref) {
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
        st->writer->traditional_xref = traditional_xref;
        st->writer->progress_cb = wasm_progress_thunk;
        st->writer->progress_user_data = &on_progress;
        res = st->writer->rewrite(ctx, st, output_path.c_str());
    }
    
    ctx->free(ctx);
    return (res == 0);
}

static bool compress_pdf_4(std::string in, std::string out, int dpi, int q) {
    return compress_pdf(in, out, dpi, q, false);
}

static bool compress_pdf_prog_5(std::string in, std::string out, int dpi, int q, val cb) {
    return compress_pdf_with_progress(in, out, dpi, q, cb, false);
}

EMSCRIPTEN_BINDINGS(pation_module) {
    function("compress_pdf", &compress_pdf_4);
    function("compress_pdf_ext", &compress_pdf);
    function("compress_pdf_with_progress", &compress_pdf_prog_5);
    function("compress_pdf_with_progress_ext", &compress_pdf_with_progress);
}

