/*
    writer.h — Pation PDF Rewriter
    Integrates image compression (img/) and font subsetting (pdf/font.h)
    to produce a minimised output PDF.
    SMALLAB28 SOFTWARE — ALL RIGHTS RESERVED
*/
#ifndef WRITER_H
#define WRITER_H

#include "pation/context.h"
#include "pation/state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pdf_writer pdf_writer;

typedef int (rewrite_pdf_fn)(pt_context *ctx, pt_state *st, const char *out_path);

typedef void (*pt_progress_callback)(int current, int total, double percent, void *user_data);

struct pdf_writer {
    rewrite_pdf_fn *rewrite;
    int target_dpi;
    int target_quality;
    pt_progress_callback progress_cb;
    void *progress_user_data;
    bool traditional_xref;
};
#define use_traditional_xref traditional_xref



void main_writer(pt_state *st);

#ifdef __cplusplus
}
#endif

#endif /* WRITER_H */
