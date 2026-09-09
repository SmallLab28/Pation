
#include "pdf/xref.h"
#include "pation/document.h"
#include "pation/state.h"
#include <stdio.h>
#include <stdlib.h>

void drop_state (pt_state *st){
    // state 
    if (st == NULL) return;

    // doc
    if (st->doc != NULL){
        if(st->doc->f != NULL){
            fclose(st->doc->f);            
        }
        free(st->doc);
    }
    // xref
    if (st->xref != NULL){
        free(st->xref);
    }
    free(st);
}


pt_state *pt_init_state(void){
    pt_state *st = (pt_state*)calloc(1, sizeof(*st));
    if (st == NULL){
        drop_state(st);
        return NULL;
    }
    // doc
    st->doc = calloc(1, sizeof(*(st->doc)));
    if(st->doc == NULL){
        free(st->doc);
        free(st);
        return NULL;
    }
    // xref
    st->xref  = calloc(1, sizeof(*(st->xref)));
    if (st->xref == NULL){
        free(st->xref);
        free(st);
        return NULL;
    }
    main_xref(st);

    st->free = drop_state;
    return st;
}
