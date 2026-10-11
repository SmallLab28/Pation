/*
    stream.h
    smallLab28
*/ 
#ifndef STREAM_H
#define STREAM_H

#include "pation/state.h"
#include "pation/context.h"
#include "miniz/miniz.h"
#include <stdio.h>
#include <vector>
#include <iostream>

typedef struct pdf_stream pdf_stream;
typedef std::vector<unsigned char> (uncompress_data_fn) (pt_context *ctx, pt_state *st,
                            const unsigned char *source, mz_ulong source_len);
typedef void(main_stream_fn) (pt_context *ctx, pt_state *st);
void main_stream(pt_context *ctx, pt_state *st);


struct pdf_stream{
    std::vector<unsigned char>dest;
    mz_ulong dest_len;
    const unsigned char *source;
    mz_ulong source_len; 
    uncompress_data_fn *uncompress;
    main_stream_fn *main;
};

#endif
