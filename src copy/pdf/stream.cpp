
#include "pdf/stream.h"
#include "pation/state.h"
#include "pation/context.h"
#include <vector>
#include <stdio.h>
#include <iostream>



std::vector<unsigned char>uncompress_data(pt_context *ctx, pt_state *st, 
                               const unsigned char *source, mz_ulong source_len){
    st->stream->dest.resize(static_cast<int> (source_len));
    while (true) {
        st->stream->dest_len = (mz_ulong) st->stream->dest.size();
        int status = mz_uncompress(st->stream->dest.data(), &st->stream->dest_len, source, source_len);
        if (status == MZ_OK) {
            st->stream->dest.resize(st->stream->dest_len);
            break;
        }
        else if (status == MZ_BUF_ERROR) {
            st->stream->dest.resize(st->stream->dest.size() * 2);
        }
        else {
            printf("ERROR CODE MINIZ: %d\n", status);
            break;
        }
    }
    return st->stream->dest;
}


void main_stream(pt_context *ctx, pt_state *st){

    st->stream->uncompress = uncompress_data;
    st->stream->main = main_stream;
}

