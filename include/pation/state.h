#ifndef STATE_H
#define STATE_H

#ifdef __cplusplus
extern "C" {
#endif
#include "pation/context.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct pt_state pt_state;
typedef struct pt_document pt_document;
typedef struct pdf_xref pdf_xref;
// Constructor
pt_state *pt_init_state (void);

typedef void (pt_drop_state) (pt_state *st);

struct pt_state {
     // struct function
     pt_document *doc;
     pdf_xref *xref;


     // state life time
     pt_drop_state *free;
};


#ifdef __cplusplus
}
#endif
#endif
