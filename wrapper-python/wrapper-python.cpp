#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <iostream>
#include "pation/context.h"
#include "pation/state.h"
#include "pation/document.h"
#include "pdf/page.h"
#include "pdf/xref.h"
#include "pdf/parser.h"
#include "pdf/stream.h"
#include "pdf/writer.h"

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

static PyObject* pation_compress(PyObject* self, PyObject* args, PyObject* kwargs) {
    const char* input_path = NULL;
    const char* output_path = NULL;
    int target_dpi = 150;
    int target_quality = 20;
    
    static const char *kwlist[] = {"input_path", "output_path", "dpi", "quality", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|ii", (char**)kwlist, 
                                     &input_path, &output_path, &target_dpi, &target_quality)) {
        return NULL;
    }

    pt_context *ctx = pt_ctx_create();
    pt_state *st = pt_load_pdf(ctx, input_path);

    if (!st) {
        ctx->free(ctx);
        PyErr_SetString(PyExc_RuntimeError, "Failed to load PDF state.");
        return NULL;
    }

    if (st && st->page && (ctx->doc == PT_DOC_TABLE || ctx->doc == PT_DOC_STREAM)) {
        st->page->page_root(ctx, st);
    }
    
    int res = -1;
    if (st && st->writer) {
        st->writer->target_dpi = target_dpi;
        st->writer->target_quality = target_quality;
        res = st->writer->rewrite(ctx, st, output_path);
    }
    
    ctx->free(ctx);

    if (res == 0) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

static PyMethodDef PationMethods[] = {
    {"compress", (PyCFunction)pation_compress, METH_VARARGS | METH_KEYWORDS, "Compress a PDF file. Usage: compress(input_path, output_path, dpi=150)"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef pationmodule = {
    PyModuleDef_HEAD_INIT,
    "pation",
    "Python wrapper for Pation PDF compressor",
    -1,
    PationMethods
};

PyMODINIT_FUNC PyInit_pation(void) {
    return PyModule_Create(&pationmodule);
}
