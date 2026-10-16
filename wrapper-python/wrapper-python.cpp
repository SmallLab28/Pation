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

static void py_progress_thunk(int current, int total, double percent, void *user_data) {
    PyObject *cb = (PyObject*)user_data;
    if (cb && PyCallable_Check(cb)) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject *arglist = Py_BuildValue("(iid)", current, total, percent);
        if (arglist) {
            PyObject *result = PyObject_CallObject(cb, arglist);
            Py_XDECREF(result);
            Py_DECREF(arglist);
        }
        PyGILState_Release(gstate);
    }
}

static PyObject* pation_compress(PyObject* self, PyObject* args, PyObject* kwargs) {
    const char* input_path = NULL;
    const char* output_path = NULL;
    int target_dpi = 150;
    int target_quality = 25;
    PyObject* progress_callback = NULL;
    int traditional_xref = 0;
    
    static const char *kwlist[] = {"input_path", "output_path", "dpi", "quality", "on_progress", "traditional_xref", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ss|iiOp", (char**)kwlist, 
                                     &input_path, &output_path, &target_dpi, &target_quality, &progress_callback, &traditional_xref)) {
        return NULL;
    }

    if (progress_callback && progress_callback != Py_None && !PyCallable_Check(progress_callback)) {
        PyErr_SetString(PyExc_TypeError, "on_progress must be callable");
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
        st->writer->traditional_xref = (traditional_xref != 0);
        if (progress_callback && progress_callback != Py_None) {
            st->writer->progress_cb = py_progress_thunk;
            st->writer->progress_user_data = progress_callback;
        } else {
            st->writer->progress_cb = nullptr;
            st->writer->progress_user_data = nullptr;
        }
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
