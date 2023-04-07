//    This is a component of AXIS, a front-end for emc
//    Copyright 2004, 2005, 2006 Jeff Epler <jepler@unpythonic.net> and
//    Chris Radek <chris@timeguy.com>
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#include <Python.h>
#include <string>
#include <map>
using namespace std;

#include "config.h"
#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"
#include "rtapi_compat.h"

#define EXCEPTION_IF_NOT_LIVE(retval) do { \
    if(self->hal_id <= 0) { \
        PyErr_SetString(PyExc_RuntimeError, "Invalid operation on closed HAL component"); \
	return retval; \
    } \
} while(0)

PyObject *to_python(hal_bit_t b) {
    return PyBool_FromLong(b);
}

PyObject *to_python(hal_u32_t u) {
    return PyLong_FromUnsignedLong(u);
}

PyObject *to_python(hal_s32_t i) {
    return PyLong_FromLong(i);
}

PyObject *to_python(hal_u64_t u) {
    return PyLong_FromUnsignedLongLong(u);
}

PyObject *to_python(hal_s64_t i) {
    return PyLong_FromLongLong(i);
}

PyObject *to_python(hal_float_t d) {
    return PyFloat_FromDouble(d);
}

bool from_python(PyObject *o, hal_float_t *d) {
    // First try more specific conversions that throw more specific
    // exceptions, e.g. OverflowError
    if (PyFloat_Check(o)) {
        *d = PyFloat_AsDouble(o);
        return true;
    } else if(PyLong_Check(o)) {
        *d = PyLong_AsDouble(o);
        return !PyErr_Occurred();
    }

    // Otherwise, use generic conversion ard catch number protocol
    // exceptions
    PyObject *tmp = PyNumber_Float(o);
    if(!tmp) {
        PyErr_Format(PyExc_TypeError,
                     "Failed to convert %s(%R) to float type",
                     Py_TYPE(o)->tp_name, o);
        return false;
    }

    *d = PyFloat_AsDouble(tmp);
    Py_XDECREF(tmp);
    return true;
}

bool from_python(PyObject *o, hal_u32_t *i) {
    // int- or float-specific conversions:  32-bit int types need
    // annoying extra checks on 64-bit, since long int is 64-bits
    if (PyLong_Check(o)) { // PyLong_AsUnsignedLong() doesn't like floats
        unsigned long ul = PyLong_AsUnsignedLong(o);
        if (PyErr_Occurred()) return false;
        if ((*i = (hal_u32_t)ul) != ul) {
            PyErr_Format(PyExc_OverflowError, "int too big to convert");
            return false;
        }
        return true;
    }

    // Other type conversions
    PyObject *tmp = PyNumber_Long(o);
    if(!tmp) {
        PyErr_Format(PyExc_TypeError,
                     "Failed to convert %s(%R) to int type",
                     Py_TYPE(o)->tp_name, o);
        return false;
    }

    *i = PyLong_AsUnsignedLong(tmp);
    if (PyErr_Occurred())
      return false;
    Py_XDECREF(tmp);
    return true;
}

bool from_python(PyObject *o, hal_s32_t *i) {
    // int- or float-specific conversions:  32-bit int types need
    // annoying extra checks on 64-bit, since long int is 64-bits
    if (PyLong_Check(o)) {
        long l = PyLong_AsLong(o);
        if (PyErr_Occurred()) return false;
        if ((*i = (hal_s32_t)l) != l) {
            PyErr_Format(PyExc_OverflowError, "int too big to convert");
            return false;
        }
        return true;
    }
    if (PyFloat_Check(o)) {
        // Python 3.10 doesn't like PyLong_AsLong(PyFloat):
        //   TypeError: 'float' object cannot be interpreted as an integer
        double tmp_double = PyFloat_AsDouble(o);
        PyObject *tmp_long = PyLong_FromDouble(tmp_double);
        long l = PyLong_AsLongLong(tmp_long);
        if (PyErr_Occurred()) return false;
        if ((*i = (hal_s32_t)l) != l) {
            PyErr_Format(PyExc_OverflowError, "int too big to convert");
            return false;
        }
        return true;
    }

    // Other type conversions
    PyObject *tmp = PyNumber_Long(o);
    if(!tmp) {
        PyErr_Format(PyExc_TypeError,
                     "Failed to convert %s(%R) to int type",
                     Py_TYPE(o)->tp_name, o);
        return false;
    }

    *i = PyLong_AsLong(tmp);
    if (PyErr_Occurred())
      return false;
    Py_XDECREF(tmp);
    return true;
}

bool from_python(PyObject *o, hal_u64_t *i) {
    // int- or float-specific conversions
    if (PyLong_Check(o)) { // PyLong_AsUnsignedLongLong() doesn't like floats
        *i = PyLong_AsUnsignedLongLong(o);
        return !PyErr_Occurred();
    }

    // Other type conversions
    PyObject *tmp = PyNumber_Long(o);
    if(!tmp) {
        PyErr_Format(PyExc_TypeError,
                     "Failed to convert %s(%R) to int type",
                     Py_TYPE(o)->tp_name, o);
        return false;
    }

    *i = PyLong_AsUnsignedLongLong(tmp);
    if (PyErr_Occurred())
      return false;
    Py_XDECREF(tmp);
    return true;
}

bool from_python(PyObject *o, hal_s64_t *i) {
    // int- or float-specific conversions
    if (PyLong_Check(o)) {
        *i = PyLong_AsLongLong(o);
        return !PyErr_Occurred();
    }
    if (PyFloat_Check(o)) {
        // Python 3.10 doesn't like PyLong_AsLongLong(PyFloat):
        //   TypeError: 'float' object cannot be interpreted as an integer
        double tmp_double = PyFloat_AsDouble(o);
        PyObject *tmp_long = PyLong_FromDouble(tmp_double);
        *i = PyLong_AsLongLong(tmp_long);
        return !PyErr_Occurred();
    }

    // Other type conversions
    PyObject *tmp = PyNumber_Long(o);
    if(!tmp) {
        PyErr_Format(PyExc_TypeError,
                     "Failed to convert %s(%R) to int type",
                     Py_TYPE(o)->tp_name, o);
        return false;
    }

    *i = PyLong_AsLongLong(tmp);
    if (PyErr_Occurred())
      return false;
    Py_XDECREF(tmp);
    return true;
}

union paramunion {
    hal_bit_t b;
    hal_u32_t u32;
    hal_s32_t s32;
    hal_u64_t u64;
    hal_s64_t s64;
    hal_float_t f;
};

union pinunion {
    void *v;
    hal_bit_t *b;
    hal_u32_t *u32;
    hal_s32_t *s32;
    hal_u64_t *u64;
    hal_s64_t *s64;
    hal_float_t *f;
};

union halunion {
    union pinunion pin;
    union paramunion param;
};

union haldirunion {
    hal_pin_dir_t pindir;
    hal_param_dir_t paramdir;
};

struct halitem {
    bool is_pin;
    hal_type_t type;
    union haldirunion dir;
    union halunion *u;
};

struct pyhalitem {
    PyObject_HEAD
    halitem  pin;
    char * name;
};

static PyObject * pyhal_pin_new(halitem * pin, const char *name);

typedef std::map<std::string, struct halitem> itemmap;

typedef struct halobject {
        PyObject_HEAD
    int hal_id;
    char *name;
    char *prefix;
    itemmap *items;
} halobject;

PyObject *pyhal_error_type = NULL;

static PyObject *pyrtapi_error(int code) {
    PyErr_SetString(pyhal_error_type, strerror(-code));
    return NULL;
}

static PyObject *pyhal_error(int code) {
    PyErr_SetString(pyhal_error_type, strerror(-code));
    return NULL;
}

static int pyhal_init(PyObject *_self, PyObject *args, PyObject *kw) {
    char *name;
    char *prefix = 0;
    halobject *self = (halobject *)_self;

    if(!PyArg_ParseTuple(args, "s|s:hal.component", &name, &prefix)) return -1;

    self->items = new itemmap();

    self->hal_id = hal_init(name);
    if(self->hal_id <= 0) {
        pyhal_error(self->hal_id);
        return -1;
    }

    self->name = strdup(name);
    self->prefix = strdup(prefix ? prefix : name);
    if(!self->name) {
        PyErr_SetString(PyExc_MemoryError, "strdup(name) failed");
        return -1;
    }
    if(!self->prefix) {
        PyErr_SetString(PyExc_MemoryError, "strdup(prefix) failed");
        return -1;
    }

    return 0;
}

static void pyhal_exit_impl(halobject *self) {
    if(self->hal_id > 0)
        hal_exit(self->hal_id);
    self->hal_id = 0;

    free(self->name);
    self->name = 0;

    free(self->prefix);
    self->prefix = 0;

    delete self->items;
    self->items = 0;
}

static void pyhal_delete(PyObject *_self) {
    halobject *self = (halobject *)_self;
    pyhal_exit_impl(self);
    Py_TYPE(self)->tp_free(self);
}

static int pyhal_write_common(halitem *pin, PyObject *value) {
    if(!pin) return -1;

    if(pin->is_pin) {
        switch(pin->type) {
            case HAL_BIT:
                *pin->u->pin.b = PyObject_IsTrue(value);
                break;
            case HAL_FLOAT: {
                double tmp;
                if(!from_python(value, &tmp)) return -1;
                *pin->u->pin.f = tmp;
                break;
            }
            case HAL_U32: {
                hal_u32_t tmp;
                // if (PyFloat_Check(value))
                //     value = PyLong_FromDouble
                if(!from_python(value, &tmp)) return -1;
                *pin->u->pin.u32 = tmp;
                break;
            }
            case HAL_S32: {
                hal_s32_t tmp;
                if(!from_python(value, &tmp)) return -1;
                *pin->u->pin.s32 = tmp;
                break;
            }
            case HAL_U64: {
                hal_u64_t tmp;
                if(!from_python(value, &tmp)) return -1;
                *pin->u->pin.u64 = tmp;
                break;
            }
            case HAL_S64: {
                hal_s64_t tmp;
                if(!from_python(value, &tmp)) return -1;
                *pin->u->pin.s64 = tmp;
                break;
            }
            default:
                PyErr_Format(pyhal_error_type, "Invalid pin type %d", pin->type);
        }
    } else {
        switch(pin->type) {
            case HAL_BIT:
                pin->u->param.b = PyObject_IsTrue(value);
                break;
            case HAL_FLOAT: {
                double tmp;
                if(!from_python(value, &tmp)) return -1;
                pin->u->param.f = tmp;
                break;
            }
            case HAL_U32: {
                hal_u32_t tmp;
                if(!from_python(value, &tmp)) return -1;
                pin->u->param.u32 = tmp;
                break;
            }
            case HAL_S32:
                hal_s32_t tmp;
                if(!from_python(value, &tmp)) return -1;
                pin->u->param.s32 = tmp;
                break;
            case HAL_U64: {
                hal_u64_t tmp;
                if(!from_python(value, &tmp)) return -1;
                *pin->u->pin.u64 = tmp;
                break;
            }
            case HAL_S64: {
                hal_s64_t tmp;
                if(!from_python(value, &tmp)) return -1;
                *pin->u->pin.s64 = tmp;
                break;
            }
            default:
                PyErr_Format(pyhal_error_type, "Invalid pin type %d", pin->type);
        }
    }
    return 0;
}

static PyObject *pyhal_read_common(halitem *item) {
    if(!item) return NULL;
    if(item->is_pin) {
        switch(item->type) {
            case HAL_BIT: return to_python(*(item->u->pin.b));
            case HAL_U32: return to_python(*(item->u->pin.u32));
            case HAL_S32: return to_python(*(item->u->pin.s32));
            case HAL_U64: return to_python(*(item->u->pin.u64));
            case HAL_S64: return to_python(*(item->u->pin.s64));
            case HAL_FLOAT: return to_python(*(item->u->pin.f));
            case HAL_TYPE_UNSPECIFIED: /* fallthrough */ ;
            case HAL_TYPE_UNINITIALIZED: /* fallthrough */ ;
            case HAL_TYPE_MAX: /* fallthrough */ ;
        }
    } else {
        switch(item->type) {
            case HAL_BIT: return to_python(item->u->param.b);
            case HAL_U32: return to_python(item->u->param.u32);
            case HAL_S32: return to_python(item->u->param.s32);
            case HAL_U64: return to_python(item->u->param.u64);
            case HAL_S64: return to_python(item->u->param.s64);
            case HAL_FLOAT: return to_python(item->u->param.f);
            case HAL_TYPE_UNSPECIFIED: /* fallthrough */ ;
            case HAL_TYPE_UNINITIALIZED: /* fallthrough */ ;
            case HAL_TYPE_MAX: /* fallthrough */ ;
        }
    }
    PyErr_Format(pyhal_error_type, "Invalid item type %d", item->type);
    return NULL;
}

static halitem *find_item(halobject *self, const char *name) {
    if(!name) return NULL;

    itemmap::iterator i = self->items->find(name);

    if(i == self->items->end()) {
        PyErr_Format(PyExc_AttributeError, "Pin '%s' does not exist", name);
        return NULL;
    }

    return &(i->second);
}

static PyObject * pyhal_create_param(halobject *self, char *name, hal_type_t type, hal_param_dir_t dir) {
    char param_name[HAL_NAME_LEN+1];
    int res;
    halitem param;
    param.is_pin = 0;

    if(type < HAL_BIT || type > HAL_U32) {
        PyErr_Format(pyhal_error_type, "Invalid param type %d", type);
        return NULL;
    }

    param.type = type;
    param.dir.paramdir = dir;
    param.u = (halunion*)hal_malloc(sizeof(halunion));
    if(!param.u) {
        PyErr_SetString(PyExc_MemoryError, "hal_malloc failed");
        return NULL;
    }

    res = snprintf(param_name, sizeof(param_name), "%s.%s", self->prefix, name);
    if(res > HAL_NAME_LEN || res < 0) { return pyhal_error(-EINVAL); }
    res = hal_param_new(param_name, type, dir, (void*)param.u, self->hal_id);
    if(res) return pyhal_error(res);

    (*self->items)[name] = param;

    return pyhal_pin_new(&param, name);
}


static PyObject * pyhal_create_pin(halobject *self, char *name, hal_type_t type, hal_pin_dir_t dir) {
    char pin_name[HAL_NAME_LEN+1];
    int res;
    halitem pin;
    pin.is_pin = 1;

    if(type <= HAL_TYPE_UNINITIALIZED || type >= HAL_TYPE_MAX) {
        PyErr_Format(pyhal_error_type, "Invalid pin type %d", type);
        return NULL;
    }

    pin.type = type;
    pin.dir.pindir = dir;
    pin.u = (halunion*)hal_malloc(sizeof(halunion));
    if(!pin.u) {
        PyErr_SetString(PyExc_MemoryError, "hal_malloc failed");
        return NULL;
    }

    res = snprintf(pin_name, sizeof(pin_name), "%s.%s", self->prefix, name);
    if(res > HAL_NAME_LEN || res < 0) {
        PyErr_Format(pyhal_error_type, "Invalid pin name length: max = %d characters",
                     HAL_NAME_LEN);
        return NULL;
    }
    res = hal_pin_new(pin_name, type, dir, (void**)pin.u, self->hal_id);
    if(res) return pyhal_error(res);

    (*self->items)[name] = pin;

    return pyhal_pin_new(&pin, name);
}

static PyObject *pyhal_new_param(PyObject *_self, PyObject *o) {
    char *name;
    int type, dir;
    halobject *self = (halobject *)_self;

    if(!PyArg_ParseTuple(o, "sii", &name, &type, &dir))
        return NULL;
    EXCEPTION_IF_NOT_LIVE(NULL);

    if (find_item(self, name)) {
        PyErr_Format(PyExc_ValueError, "Duplicate item name '%s'", name);
        return NULL;
    } else { PyErr_Clear(); }
    return pyhal_create_param(self, name, (hal_type_t)type, (hal_param_dir_t)dir);
}


static PyObject *pyhal_new_pin(PyObject *_self, PyObject *o) {
    char *name;
    int type, dir;
    halobject *self = (halobject *)_self;

    if(!PyArg_ParseTuple(o, "sii", &name, &type, &dir))
        return NULL;
    EXCEPTION_IF_NOT_LIVE(NULL);

    if (find_item(self, name)) {
        PyErr_Format(PyExc_ValueError, "Duplicate item name '%s'", name);
        return NULL;
    } else { PyErr_Clear(); }
    return pyhal_create_pin(self, name, (hal_type_t)type, (hal_pin_dir_t)dir);
}

static PyObject *pyhal_get_pin(PyObject *_self, PyObject *o) {
    char *name;
    halobject *self = (halobject *)_self;

    if(!PyArg_ParseTuple(o, "s", &name))
        return NULL;
    EXCEPTION_IF_NOT_LIVE(NULL);

    halitem * pin = find_item(self, name);
    if (!pin)
	return NULL;
    return pyhal_pin_new(pin, name);
}

static PyObject *pyhal_ready(PyObject *_self, PyObject *o) {
    // hal_ready did not exist in EMC 2.0.x, make it a no-op
    halobject *self = (halobject *)_self;
    EXCEPTION_IF_NOT_LIVE(NULL);
    int res = hal_ready(self->hal_id);
    if(res) return pyhal_error(res);
    Py_RETURN_NONE;
}

static PyObject *pyhal_exit(PyObject *_self, PyObject *o) {
    halobject *self = (halobject *)_self;
    pyhal_exit_impl(self);
    Py_RETURN_NONE;
}

static PyObject *pyhal_repr(PyObject *_self) {
    halobject *self = (halobject *)_self;
    return PyUnicode_FromFormat("<hal component %s(%d) with %zu pins and params>",
            self->name, self->hal_id, self->items->size());
}

static PyObject *pyhal_getattro(PyObject *_self, PyObject *attro)  {
    PyObject *result;
    halobject *self = (halobject *)_self;
    EXCEPTION_IF_NOT_LIVE(NULL);

    result = PyObject_GenericGetAttr((PyObject*)self, attro);
    if(result) return result;

    PyErr_Clear();
    return pyhal_read_common(find_item(self, PyUnicode_AsUTF8(attro)));
}

static int pyhal_setattro(PyObject *_self, PyObject *attro, PyObject *v) {
    halobject *self = (halobject *)_self;
    EXCEPTION_IF_NOT_LIVE(-1);
    return pyhal_write_common(find_item(self, PyUnicode_AsUTF8(attro)), v);
}

static Py_ssize_t pyhal_len(PyObject *_self) {
    halobject* self = (halobject*)_self;
    EXCEPTION_IF_NOT_LIVE(-1);
    return self->items->size();
}

static PyObject *pyhal_get_prefix(PyObject *_self, PyObject *args) {
    halobject* self = (halobject*)_self;
    if(!PyArg_ParseTuple(args, "")) return NULL;
    EXCEPTION_IF_NOT_LIVE(NULL);

    if(!self->prefix)
	Py_RETURN_NONE;

    return PyUnicode_FromString(self->prefix);
}


static PyObject *pyhal_set_prefix(PyObject *_self, PyObject *args) {
    char *newprefix;
    halobject* self = (halobject*)_self;
    if(!PyArg_ParseTuple(args, "s", &newprefix)) return NULL;
    EXCEPTION_IF_NOT_LIVE(NULL);

    if(self->prefix)
        free(self->prefix);
    self->prefix = strdup(newprefix);

    if(!self->prefix) {
        PyErr_SetString(PyExc_MemoryError, "strdup(prefix) failed");
        return NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef hal_methods[] = {
    {"setprefix", pyhal_set_prefix, METH_VARARGS,
        "Set the prefix for newly created pins and parameters"},
    {"getprefix", pyhal_get_prefix, METH_VARARGS,
        "Get the prefix for newly created pins and parameters"},
    {"newparam", pyhal_new_param, METH_VARARGS,
        "Create a new parameter"},
    {"newpin", pyhal_new_pin, METH_VARARGS,
        "Create a new pin"},
    {"getitem", pyhal_get_pin, METH_VARARGS,
        "Get existing pin object"},
    {"exit", pyhal_exit, METH_NOARGS,
        "Call hal_exit"},
    {"ready", pyhal_ready, METH_NOARGS,
        "Call hal_ready"},
    {NULL},
};

static PyMappingMethods halobject_map = {
    pyhal_len,
    pyhal_getattro,
    pyhal_setattro
};

static
PyTypeObject halobject_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "hal.component",           /*tp_name*/
    sizeof(halobject),         /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    pyhal_delete,              /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    pyhal_repr,                /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    &halobject_map,            /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    pyhal_getattro,            /*tp_getattro*/
    pyhal_setattro,            /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,        /*tp_flags*/
    "HAL Component",           /*tp_doc*/
    0,                         /*tp_traverse*/
    0,                         /*tp_clear*/
    0,                         /*tp_richcompare*/
    0,                         /*tp_weaklistoffset*/
    0,                         /*tp_iter*/
    0,                         /*tp_iternext*/
    hal_methods,               /*tp_methods*/
    0,                         /*tp_members*/
    0,                         /*tp_getset*/
    0,                         /*tp_base*/
    0,                         /*tp_dict*/
    0,                         /*tp_descr_get*/
    0,                         /*tp_descr_set*/
    0,                         /*tp_dictoffset*/
    pyhal_init,                /*tp_init*/
    0,                         /*tp_alloc*/
    PyType_GenericNew,         /*tp_new*/
    0,                         /*tp_free*/
    0,                         /*tp_is_gc*/
};

static const char * pin_type2name(hal_type_t type) {
    switch (type) {
	case HAL_BIT: return "BIT";
	case HAL_S32: return "S32";
	case HAL_U32: return "U32";
	case HAL_S64: return "S64";
	case HAL_U64: return "U64";
	case HAL_FLOAT: return "FLOAT";
	default: return "unknown";
    }
}

static const char * pin_dir2name(hal_pin_dir_t type) {
    switch (type) {
	case HAL_IN:  return "IN";
	case HAL_IO:  return "IO";
	case HAL_OUT: return "OUT";
	default: return "unknown";
    }
}

static const char * param_dir2name(hal_param_dir_t type) {
    switch (type) {
	case HAL_RO:  return "RO";
	case HAL_RW:  return "RW";
	default: return "unknown";
    }
}

static PyObject *pyhalpin_repr(PyObject *_self) {
    pyhalitem *pyself = (pyhalitem *) _self;
    halitem *self = &pyself->pin;

    const char * name = "(null)";
    if (pyself->name) name = pyself->name;

    if (!self->is_pin)
	return PyUnicode_FromFormat("<hal param \"%s\" %s-%s>", name,
	    pin_type2name(self->type), param_dir2name(self->dir.paramdir));
    return PyUnicode_FromFormat("<hal pin \"%s\" %s-%s>", name,
            pin_type2name(self->type), pin_dir2name(self->dir.pindir));
}

static int pyhalpin_init(PyObject *_self, PyObject *, PyObject *) {
    PyErr_Format(PyExc_RuntimeError,
	    "Cannot be constructed directly");
    return -1;
}

static void pyhalpin_delete(PyObject *_self) {
    pyhalitem *self = (pyhalitem *)_self;

    if(self->name) free(self->name);

    PyObject_Del(self);
}

static PyObject * pyhal_pin_set(PyObject * _self, PyObject * value) {
    pyhalitem * self = (pyhalitem *) _self;
    // if ((self->pin.type == HAL_S32) ||
    // PyLong_FromDouble(value);
    if (pyhal_write_common(&self->pin, value) == -1)
	return NULL;
    Py_RETURN_NONE;
}

static PyObject * pyhal_pin_get(PyObject * _self, PyObject *) {
    pyhalitem * self = (pyhalitem *) _self;
    return pyhal_read_common(&self->pin);
}

static PyObject * pyhal_pin_get_type(PyObject * _self, PyObject *) {
    pyhalitem * self = (pyhalitem *) _self;
    return PyLong_FromLong(self->pin.type);
}

static PyObject * pyhal_pin_get_dir(PyObject * _self, PyObject *) {
    pyhalitem * self = (pyhalitem *) _self;
    if (self->pin.is_pin)
	return PyLong_FromLong(self->pin.dir.pindir);
    else
	return PyLong_FromLong(self->pin.dir.paramdir);
}

static PyObject * pyhal_pin_is_pin(PyObject * _self, PyObject *) {
    pyhalitem * self = (pyhalitem *) _self;
    return PyBool_FromLong(self->pin.is_pin);
}

static PyObject * pyhal_pin_get_name(PyObject * _self, PyObject *) {
    pyhalitem * self = (pyhalitem *) _self;
    if (!self->name)
	Py_RETURN_NONE;
    return PyUnicode_FromString(self->name);
}

static PyMethodDef halpin_methods[] = {
    {"set", pyhal_pin_set, METH_O, "Set item value"},
    {"get", pyhal_pin_get, METH_NOARGS, "Get item value"},
    {"get_type", pyhal_pin_get_type, METH_NOARGS, "Get item type"},
    {"get_dir", pyhal_pin_get_dir, METH_NOARGS, "Get item direction"},
    {"get_name", pyhal_pin_get_name, METH_NOARGS, "Get item name"},
    {"is_pin", pyhal_pin_is_pin, METH_NOARGS, "If item is pin or param"},
    {NULL},
};

static
PyTypeObject halpin_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "hal.item",                /*tp_name*/
    sizeof(pyhalitem),         /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    pyhalpin_delete,           /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    pyhalpin_repr,             /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "HAL Pin",                 /*tp_doc*/
    0,                         /*tp_traverse*/
    0,                         /*tp_clear*/
    0,                         /*tp_richcompare*/
    0,                         /*tp_weaklistoffset*/
    0,                         /*tp_iter*/
    0,                         /*tp_iternext*/
    halpin_methods,            /*tp_methods*/
    0,                         /*tp_members*/
    0,                         /*tp_getset*/
    0,                         /*tp_base*/
    0,                         /*tp_dict*/
    0,                         /*tp_descr_get*/
    0,                         /*tp_descr_set*/
    0,                         /*tp_dictoffset*/
    pyhalpin_init,             /*tp_init*/
    0,                         /*tp_alloc*/
    PyType_GenericNew,         /*tp_new*/
    0,                         /*tp_free*/
    0,                         /*tp_is_gc*/
};

static PyObject * pyhal_pin_new(halitem * pin, const char * name) {
    pyhalitem * pypin = PyObject_New(pyhalitem, &halpin_type);
    if (!pypin)
	return NULL;
    pypin->pin = *pin;
    if (name)
	pypin->name = strdup(name);
    else
	pypin->name = NULL;

    return (PyObject *) pypin;
}

PyObject *pin_has_writer(PyObject *self, PyObject *args) {
    char *name;
    if(!PyArg_ParseTuple(args, "s", &name)) return NULL;
    if(!SHMPTR(0)) {
	PyErr_Format(PyExc_RuntimeError,
		"Cannot call before creating component");
	return NULL;
    }

    hal_pin_t *pin = halpr_find_pin_by_name(name);
    if(!pin) {
	PyErr_Format(PyExc_NameError, "Pin `%s' does not exist", name);
	return NULL;
    }

    if(pin_is_linked(pin)) {
	hal_sig_t *signal = signal_of(pin);
	return PyBool_FromLong(signal->writers > 0);
    }
    Py_INCREF(Py_False);
    return Py_False;
}


PyObject *component_exists(PyObject *self, PyObject *args) {
    char *name;
    if(!PyArg_ParseTuple(args, "s", &name)) return NULL;
    if(!SHMPTR(0)) {
	PyErr_Format(PyExc_RuntimeError,
		"Cannot call before creating component");
	return NULL;
    }

    return PyBool_FromLong(halpr_find_comp_by_name(name) != NULL);
}

PyObject *component_is_ready(PyObject *self, PyObject *args) {
    char *name;
    if(!PyArg_ParseTuple(args, "s", &name)) return NULL;
    if(!SHMPTR(0)) {
	PyErr_Format(PyExc_RuntimeError,
		"Cannot call before creating component");
	return NULL;
    }

    return PyBool_FromLong(halpr_find_comp_by_name(name)->state > COMP_INITIALIZING);
}

PyObject *new_sig(PyObject *self, PyObject *args) {
    char *name;
    int type,retval;
    if(!PyArg_ParseTuple(args, "si", &name,&type)) return NULL;
    if(!SHMPTR(0)) {
	PyErr_Format(PyExc_RuntimeError,
		"Cannot call before creating component");
	return NULL;
    }
    //printf("INFO HALMODULE -- make signal -> %s type %d\n",name,(hal_type_t) type);
    switch (type) {
	case HAL_BIT:
        retval = hal_signal_new(name, HAL_BIT);
        break;
	case HAL_S32:
        retval = hal_signal_new(name, HAL_S32);
        break;
	case HAL_U32:
        retval = hal_signal_new(name, HAL_U32);
        break;
	case HAL_S64:
        retval = hal_signal_new(name, HAL_S64);
        break;
	case HAL_U64:
        retval = hal_signal_new(name, HAL_U64);
        break;
	case HAL_FLOAT:
        retval = hal_signal_new(name, HAL_FLOAT);
        break;
	default: { PyErr_Format(PyExc_RuntimeError,
		"not a valid HAL signal type");
	return NULL;}
    }
    return PyBool_FromLong(retval != 0);
}

PyObject *connect(PyObject *self, PyObject *args) {
    char *signame,*pinname;
    if(!PyArg_ParseTuple(args, "ss", &pinname,&signame)) return NULL;
    if(!SHMPTR(0)) {
	PyErr_Format(PyExc_RuntimeError,
		"Cannot call before creating component");
	return NULL;
    }
    //printf("INFO HALMODULE -- link sig %s to pin %s\n",signame,pinname);
    return PyBool_FromLong(hal_link(pinname, signame) != 0);
}

static int set_common(hal_type_t type, void *d_ptr, char *value) {
    // This function assumes that the mutex is held
    int retval = 0;
    double fval;
    long lval;
    unsigned long ulval;
    long long llval;
    unsigned long long ullval;
    char *cp = value;

    switch (type) {
    case HAL_BIT:
	if ((strcmp("1", value) == 0) || (strcasecmp("TRUE", value) == 0)) {
	    *(hal_bit_t *) (d_ptr) = 1;
	} else if ((strcmp("0", value) == 0)
	    || (strcasecmp("FALSE", value)) == 0) {
	    *(hal_bit_t *) (d_ptr) = 0;
	} else {

	    retval = -EINVAL;
	}
	break;
    case HAL_FLOAT:
	fval = strtod ( value, &cp );
	if ((*cp != '\0') && (!isspace(*cp))) {
	    // invalid character(s) in string

	    retval = -EINVAL;
	} else {
	    *((hal_float_t *) (d_ptr)) = fval;
	}
	break;
    case HAL_S32:
	lval = strtol(value, &cp, 0);
	if ((*cp != '\0') && (!isspace(*cp))) {
	    // invalid chars in string

	    retval = -EINVAL;
	} else {
	    *((hal_s32_t *) (d_ptr)) = lval;
	}
	break;
    case HAL_U32:
	ulval = strtoul(value, &cp, 0);
	if ((*cp != '\0') && (!isspace(*cp))) {
	    // invalid chars in string

	    retval = -EINVAL;
	} else {
	    *((hal_u32_t *) (d_ptr)) = ulval;
	}
	break;
    case HAL_S64:
	llval = strtoll(value, &cp, 0);
	if ((*cp != '\0') && (!isspace(*cp))) {
	    // invalid chars in string

	    retval = -EINVAL;
	} else {
	    *((hal_s64_t *) (d_ptr)) = llval;
	}
	break;
    case HAL_U64:
	ullval = strtoull(value, &cp, 0);
	if ((*cp != '\0') && (!isspace(*cp))) {
	    // invalid chars in string

	    retval = -EINVAL;
	} else {
	    *((hal_u64_t *) (d_ptr)) = ullval;
	}
	break;
    default:
	// Shouldn't get here, but just in case...

	retval = -EINVAL;
    }
    return retval;
}

PyObject *set_p(PyObject *self, PyObject *args) {
    char *name,*value;
    int retval;
    hal_param_t *param;
    hal_pin_t *pin;
    hal_type_t type;
    void *d_ptr;

    if(!PyArg_ParseTuple(args, "ss", &name,&value)) return NULL;
    if(!SHMPTR(0)) {
	PyErr_Format(PyExc_RuntimeError,
		"Cannot call before creating component");
	return NULL;
    }
    //printf("INFO HALMODULE -- settting pin / param - name:%s value:%s\n",name,value);
    // get mutex before accessing shared data
    rtapi_mutex_get(&(hal_data->mutex));
    // search param list for name
    param = halpr_find_param_by_name(name);
    if (param == 0) {
        pin = halpr_find_pin_by_name(name);
        if(pin == 0) {
            rtapi_mutex_give(&(hal_data->mutex));

            PyErr_Format(PyExc_RuntimeError,
		        "pin not found");
	        return NULL;
        } else {
            // found it
            type = pin->type;
            if(pin->dir == HAL_OUT) {
                rtapi_mutex_give(&(hal_data->mutex));

                PyErr_Format(PyExc_RuntimeError,
		            "pin not writable");
	            return NULL;
            }
            if(pin_is_linked(pin)) {
                rtapi_mutex_give(&(hal_data->mutex));

                PyErr_Format(PyExc_RuntimeError,
		            "pin connected to signal");
	            return NULL;
            }
            d_ptr = (void*)&pin->dummysig;
        }
    } else {
        // found it
        type = param->type;
        /* is it read only? */
        if (param->dir == HAL_RO) {
            rtapi_mutex_give(&(hal_data->mutex));

            PyErr_Format(PyExc_RuntimeError,
		        "param not writable");
	        return NULL;
        }
        d_ptr = SHMPTR(param->data_ptr);
    }
    retval = set_common(type, d_ptr, value);
    rtapi_mutex_give(&(hal_data->mutex));
    return PyBool_FromLong(retval != 0);
}

struct shmobject {
    PyObject_HEAD
    halobject *comp;
    int key;
    int shm_id;
    unsigned long size;
    void *buf;
};

static int pyshm_init(PyObject *_self, PyObject *args, PyObject *kw) {
    shmobject *self = (shmobject *)_self;
    self->comp = 0;
    self->shm_id = -1;

    if(!PyArg_ParseTuple(args, "O!ik",
		&halobject_type, &self->comp, &self->key, &self->size))
	return -1;

    self->shm_id = rtapi_shmem_new(self->key, self->comp->hal_id, self->size);
    if(self->shm_id < 0) {
	self->comp = 0;
	self->size = 0;
	pyrtapi_error(self->shm_id);
	return -1;
    }

    rtapi_shmem_getptr(self->shm_id, &self->buf);
    Py_INCREF(self->comp);

    return 0;
}

static void pyshm_delete(PyObject *_self) {
    shmobject *self = (shmobject *)_self;
    if(self->comp && self->shm_id > 0)
	rtapi_shmem_delete(self->shm_id, self->comp->hal_id);
    Py_XDECREF(self->comp);
}
#if PY_MAJOR_VERSION >=3
static int
shm_buffer_getbuffer(PyObject *obj, Py_buffer *view, int flags)
{
if (view == NULL) {
    PyErr_SetString(PyExc_ValueError, "NULL view in getbuffer");
    return -1;
  }
  shmobject* self = (shmobject *)obj;
  view->obj = (PyObject*)self;
  view->buf = (void*)self->buf;
  view->len = self->size;
  view->readonly = 0;
  Py_INCREF(self);  // need to increase the reference count
  return 0;
}
#else

static Py_ssize_t shm_buffer(PyObject *_self, Py_ssize_t segment, void **ptrptr){
    shmobject *self = (shmobject *)_self;
    if(ptrptr) *ptrptr = self->buf;
    return self->size;
}
static Py_ssize_t shm_segcount(PyObject *_self, Py_ssize_t *lenp) {
    shmobject *self = (shmobject *)_self;
    if(lenp) *lenp = self->size;
    return 1;
}

#endif
static PyObject *pyshm_repr(PyObject *_self) {
    shmobject *self = (shmobject *)_self;
    return PyUnicode_FromFormat("<shared memory buffer key=%08x id=%d size=%zu>",
	    self->key, self->shm_id, self->size);
}

static PyObject *shm_setsize(PyObject *_self, PyObject *args) {
    shmobject *self = (shmobject *)_self;
    if(!PyArg_ParseTuple(args, "k", &self->size)) return NULL;
    Py_RETURN_NONE;
}


static PyObject *shm_getbuffer(PyObject *_self, PyObject *dummy) {

    shmobject *self = (shmobject *)_self;
    return (PyObject*)PyMemoryView_FromObject((PyObject*)self);
}

static PyObject *set_msg_level(PyObject *_self, PyObject *args) {
    int level, res;
    if(!PyArg_ParseTuple(args, "i", &level)) return NULL;
    res = rtapi_set_msg_level(level);
    if(res) return pyhal_error(res);
    Py_RETURN_NONE;
}

static PyObject *get_msg_level(PyObject *_self, PyObject *args) {
    return PyLong_FromLong(rtapi_get_msg_level());
}


#if PY_MAJOR_VERSION >=3

static PyBufferProcs shmbuffer_procs = {
    (getbufferproc)shm_buffer_getbuffer,         /* bf_getbuffer */
    (releasebufferproc)NULL, //(releasebufferproc)shm_buffer_releasebuffer, /* bf_releasebuffer */
};

#else

static
PyBufferProcs shmbuffer_procs = {
    shm_buffer,
    shm_buffer,
    shm_segcount,
    NULL
};

#endif

static PyMethodDef shm_methods[] = {
    {"getbuffer", shm_getbuffer, METH_NOARGS,
	"Get a writable buffer object for the shared memory segment"},
    {"setsize", shm_setsize, METH_VARARGS,
	"Set the size of the shared memory segment"},
    {NULL},
};

static
PyTypeObject shm_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "hal.shm",                 /*tp_name*/
    sizeof(shmobject),         /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)pyshm_delete,  /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    (reprfunc)pyshm_repr,      /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    &shmbuffer_procs,          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "HAL Shared Memory",       /*tp_doc*/
    0,                         /*tp_traverse*/
    0,                         /*tp_clear*/
    0,                         /*tp_richcompare*/
    0,                         /*tp_weaklistoffset*/
    0,                         /*tp_iter*/
    0,                         /*tp_iternext*/
    shm_methods,               /*tp_methods*/
    0,                         /*tp_members*/
    0,                         /*tp_getset*/
    0,                         /*tp_base*/
    0,                         /*tp_dict*/
    0,                         /*tp_descr_get*/
    0,                         /*tp_descr_set*/
    0,                         /*tp_dictoffset*/
    (initproc)pyshm_init,      /*tp_init*/
    0,                         /*tp_alloc*/
    PyType_GenericNew,         /*tp_new*/
    0,                         /*tp_free*/
    0,                         /*tp_is_gc*/
};


PyMethodDef module_methods[] = {
    {"pin_has_writer", pin_has_writer, METH_VARARGS,
	"Return a FALSE value if a pin has no writers and TRUE if it does"},
    {"component_exists", component_exists, METH_VARARGS,
	"Return a TRUE value if the named component exists"},
    {"component_is_ready", component_is_ready, METH_VARARGS,
	"Return a TRUE value if the named component is ready"},
    {"set_msg_level", set_msg_level, METH_VARARGS,
	"Set the RTAPI message level"},
    {"get_msg_level", get_msg_level, METH_NOARGS,
	"Get the RTAPI message level"},
    {"new_sig", new_sig, METH_VARARGS,
	"create a signal"},
    {"connect", connect, METH_VARARGS,
	"connect pin to signal"},
    {"set_p", set_p, METH_VARARGS,
	"set pin value"},
    {NULL},
};

const char *module_doc = "Interface to hal\n"
"\n"
"This module allows the creation of userspace HAL components in Python.\n"
"This includes pins and parameters of the various HAL types.\n"
"\n"
"Typical usage:\n"
"\n"
"import hal, time\n"
"h = hal.component(\"component-name\")\n"
"# create pins and parameters with calls to h.newpin and h.newparam\n"
"h.newpin(\"in\", hal.HAL_FLOAT, hal.HAL_IN)\n"
"h.newpin(\"out\", hal.HAL_FLOAT, hal.HAL_OUT)\n"
"h.ready() # mark the component as 'ready'\n"
"\n"
"try:\n"
"    while 1:\n"
"        # act on changed input pins; update values on output pins\n"
"        time.sleep(1)\n"
"        h['out'] = h['in']\n"
"except KeyboardInterrupt: pass"
"\n"
"\n"
"When the component is requested to exit with 'halcmd unload', a\n"
"KeyboardInterrupt exception will be raised."
;

static struct PyModuleDef hal_moduledef = {
    PyModuleDef_HEAD_INIT,  /* m_base */
    "_hal",                 /* m_name */
    module_doc,                   /* m_doc */
    -1,                     /* m_size */
    module_methods            /* m_methods */
};

PyMODINIT_FUNC PyInit__hal(void)
{
    PyObject *m = PyModule_Create(&hal_moduledef);

    pyhal_error_type = PyErr_NewException((char*)"hal.error", NULL, NULL);
    PyModule_AddObject(m, "error", pyhal_error_type);

    PyType_Ready(&halobject_type);
    PyType_Ready(&shm_type);
    PyType_Ready(&halpin_type);
    PyModule_AddObject(m, "component", (PyObject*)&halobject_type);
    PyModule_AddObject(m, "shm", (PyObject*)&shm_type);
    PyModule_AddObject(m, "item", (PyObject*)&halpin_type);

    PyModule_AddIntConstant(m, "MSG_NONE", RTAPI_MSG_NONE);
    PyModule_AddIntConstant(m, "MSG_ERR", RTAPI_MSG_ERR);
    PyModule_AddIntConstant(m, "MSG_WARN", RTAPI_MSG_WARN);
    PyModule_AddIntConstant(m, "MSG_INFO", RTAPI_MSG_INFO);
    PyModule_AddIntConstant(m, "MSG_DBG", RTAPI_MSG_DBG);
    PyModule_AddIntConstant(m, "MSG_ALL", RTAPI_MSG_ALL);

    PyModule_AddIntConstant(m, "HAL_BIT", HAL_BIT);
    PyModule_AddIntConstant(m, "HAL_FLOAT", HAL_FLOAT);
    PyModule_AddIntConstant(m, "HAL_S32", HAL_S32);
    PyModule_AddIntConstant(m, "HAL_U32", HAL_U32);
    PyModule_AddIntConstant(m, "HAL_S64", HAL_S64);
    PyModule_AddIntConstant(m, "HAL_U64", HAL_U64);

    PyModule_AddIntConstant(m, "HAL_RO", HAL_RO);
    PyModule_AddIntConstant(m, "HAL_RW", HAL_RW);
    PyModule_AddIntConstant(m, "HAL_IN", HAL_IN);
    PyModule_AddIntConstant(m, "HAL_OUT", HAL_OUT);
    PyModule_AddIntConstant(m, "HAL_IO", HAL_IO);

    PyRun_SimpleString(
            "(lambda s=__import__('signal'):"
                 "s.signal(s.SIGTERM, s.default_int_handler))()");
    return m;
}
