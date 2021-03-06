/**
 * Copyright (c) 2016-present, Gregory Szorc
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license. See the LICENSE file for details.
 */

#include "python-zstandard.h"
#include "blocks_output_buffer.h"

extern PyObject *ZstdError;

static void ZstdCompressionObj_dealloc(ZstdCompressionObj *self) {
    PyMem_Free(self->output.dst);
    self->output.dst = NULL;

    Py_XDECREF(self->compressor);

    PyObject_Del(self);
}

static PyObject *ZstdCompressionObj_compress(ZstdCompressionObj *self,
                                             PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"data", NULL};

    Py_buffer source;
    ZSTD_inBuffer input;
    ZSTD_outBuffer output;
    BlocksOutputBuffer blocks_buffer;
    size_t zresult;
    PyObject *result;

    if (self->finished) {
        PyErr_SetString(ZstdError,
                        "cannot call compress() after compressor finished");
        return NULL;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y*:compress", kwlist,
                                     &source)) {
        return NULL;
    }

    input.src = source.buf;
    input.size = source.len;
    input.pos = 0;

    /* Initialize blocks output buffer before any `goto except` statement. */
    if (OutputBuffer_InitAndGrow(&blocks_buffer, &output, -1) < 0) {
        goto except;
    }

    while (input.pos < (size_t)source.len) {
        Py_BEGIN_ALLOW_THREADS zresult = ZSTD_compressStream2(
            self->compressor->cctx, &output, &input, ZSTD_e_continue);
        Py_END_ALLOW_THREADS

            if (ZSTD_isError(zresult)) {
            PyErr_Format(ZstdError, "zstd compress error: %s",
                         ZSTD_getErrorName(zresult));
            goto except;
        }

        if (output.pos == output.size) {
            if (OutputBuffer_Grow(&blocks_buffer, &output) < 0) {
                goto except;
            }
        }
    }

    result = OutputBuffer_Finish(&blocks_buffer, &output);
    if (result) {
        goto finally;
    }

except:
    OutputBuffer_OnError(&blocks_buffer);
    result = NULL;

finally:
    PyBuffer_Release(&source);

    return result;
}

static PyObject *ZstdCompressionObj_flush(ZstdCompressionObj *self,
                                          PyObject *args, PyObject *kwargs) {
    static char *kwlist[] = {"flush_mode", NULL};

    int flushMode = compressorobj_flush_finish;
    size_t zresult;
    PyObject *result;
    ZSTD_inBuffer input;
    ZSTD_outBuffer output;
    BlocksOutputBuffer blocks_buffer;
    ZSTD_EndDirective zFlushMode;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i:flush", kwlist,
                                     &flushMode)) {
        return NULL;
    }

    if (flushMode != compressorobj_flush_finish &&
        flushMode != compressorobj_flush_block) {
        PyErr_SetString(PyExc_ValueError, "flush mode not recognized");
        return NULL;
    }

    if (self->finished) {
        PyErr_SetString(ZstdError, "compressor object already finished");
        return NULL;
    }

    switch (flushMode) {
    case compressorobj_flush_block:
        zFlushMode = ZSTD_e_flush;
        break;

    case compressorobj_flush_finish:
        zFlushMode = ZSTD_e_end;
        self->finished = 1;
        break;

    default:
        PyErr_SetString(ZstdError, "unhandled flush mode");
        return NULL;
    }

    assert(self->output.pos == 0);

    input.src = NULL;
    input.size = 0;
    input.pos = 0;

    /* Initialize blocks output buffer before any `goto except` statement. */
    if (OutputBuffer_InitAndGrow(&blocks_buffer, &output, -1) < 0) {
        goto except;
    }

    while (1) {
        Py_BEGIN_ALLOW_THREADS zresult = ZSTD_compressStream2(
            self->compressor->cctx, &output, &input, zFlushMode);
        Py_END_ALLOW_THREADS

            if (ZSTD_isError(zresult)) {
            PyErr_Format(ZstdError, "error ending compression stream: %s",
                         ZSTD_getErrorName(zresult));
            goto except;
        }

        if (!zresult) {
            break;
        }

        if (output.pos == output.size) {
            if (OutputBuffer_Grow(&blocks_buffer, &output) < 0) {
                goto except;
            }
        }
    }

    result = OutputBuffer_Finish(&blocks_buffer, &output);
    if (result) {
        return result;
    }

except:
    OutputBuffer_OnError(&blocks_buffer);
    return NULL;
}

static PyMethodDef ZstdCompressionObj_methods[] = {
    {"compress", (PyCFunction)ZstdCompressionObj_compress,
     METH_VARARGS | METH_KEYWORDS, PyDoc_STR("compress data")},
    {"flush", (PyCFunction)ZstdCompressionObj_flush,
     METH_VARARGS | METH_KEYWORDS, PyDoc_STR("finish compression operation")},
    {NULL, NULL}};

PyTypeObject ZstdCompressionObjType = {
    PyVarObject_HEAD_INIT(NULL, 0) "zstd.ZstdCompressionObj", /* tp_name */
    sizeof(ZstdCompressionObj),                               /* tp_basicsize */
    0,                                                        /* tp_itemsize */
    (destructor)ZstdCompressionObj_dealloc,                   /* tp_dealloc */
    0,                                                        /* tp_print */
    0,                                                        /* tp_getattr */
    0,                                                        /* tp_setattr */
    0,                                                        /* tp_compare */
    0,                                                        /* tp_repr */
    0,                                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    0,                                        /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    0,                                        /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    ZstdCompressionObj_methods,               /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    0,                                        /* tp_alloc */
    PyType_GenericNew,                        /* tp_new */
};

void compressobj_module_init(PyObject *module) {
    Py_SET_TYPE(&ZstdCompressionObjType, &PyType_Type);
    if (PyType_Ready(&ZstdCompressionObjType) < 0) {
        return;
    }
}
