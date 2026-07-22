#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "webview/webview.h"

#include <exception>
#include <memory>

namespace {

constexpr const char *kCapsuleName = "appverse.WindowHandle";

struct WindowHandle {
  std::unique_ptr<webview::webview> window;
};

WindowHandle *get_handle(PyObject *capsule) {
  auto *handle = static_cast<WindowHandle *>(PyCapsule_GetPointer(capsule, kCapsuleName));
  if (!handle) {
    return nullptr;
  }
  if (!handle->window) {
    PyErr_SetString(PyExc_RuntimeError, "window has already been destroyed");
    return nullptr;
  }
  return handle;
}

void capsule_destructor(PyObject *capsule) {
  auto *handle = static_cast<WindowHandle *>(PyCapsule_GetPointer(capsule, kCapsuleName));
  delete handle;
}

PyObject *native_create_window(PyObject *, PyObject *args, PyObject *kwargs) {
  int debug = 0;
  static const char *keywords[] = {"debug", nullptr};

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|p", const_cast<char **>(keywords),
                                   &debug)) {
    return nullptr;
  }

  try {
    auto *handle = new WindowHandle{
        std::make_unique<webview::webview>(debug != 0, nullptr)};
    return PyCapsule_New(handle, kCapsuleName, capsule_destructor);
  } catch (const std::exception &e) {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    return nullptr;
  }
}

PyObject *native_destroy(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  handle->window.reset();
  Py_RETURN_NONE;
}

PyObject *native_set_title(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *title = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &title)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  handle->window->set_title(title);
  Py_RETURN_NONE;
}

PyObject *native_set_size(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int width = 0;
  int height = 0;
  int hint = WEBVIEW_HINT_NONE;

  if (!PyArg_ParseTuple(args, "Oii|i", &capsule, &width, &height, &hint)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  handle->window->set_size(width, height, static_cast<webview_hint_t>(hint));
  Py_RETURN_NONE;
}

PyObject *native_set_html(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *html = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &html)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  handle->window->set_html(html);
  Py_RETURN_NONE;
}

PyObject *native_navigate(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *url = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &url)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  handle->window->navigate(url);
  Py_RETURN_NONE;
}

PyObject *native_init(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *js = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &js)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  handle->window->init(js);
  Py_RETURN_NONE;
}

PyObject *native_eval(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *js = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &js)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  handle->window->eval(js);
  Py_RETURN_NONE;
}

PyObject *native_run(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  Py_BEGIN_ALLOW_THREADS
  handle->window->run();
  Py_END_ALLOW_THREADS
  Py_RETURN_NONE;
}

PyMethodDef methods[] = {
    {"create_window", reinterpret_cast<PyCFunction>(native_create_window),
     METH_VARARGS | METH_KEYWORDS, "Create a native webview window handle."},
    {"destroy", reinterpret_cast<PyCFunction>(native_destroy), METH_VARARGS,
     "Destroy a native webview window handle."},
    {"set_title", reinterpret_cast<PyCFunction>(native_set_title), METH_VARARGS,
     "Set the native window title."},
    {"set_size", reinterpret_cast<PyCFunction>(native_set_size), METH_VARARGS,
     "Set the native window size."},
    {"set_html", reinterpret_cast<PyCFunction>(native_set_html), METH_VARARGS,
     "Render an HTML string."},
    {"navigate", reinterpret_cast<PyCFunction>(native_navigate), METH_VARARGS,
     "Navigate to a URL."},
    {"init", reinterpret_cast<PyCFunction>(native_init), METH_VARARGS,
     "Inject JavaScript before page scripts run."},
    {"eval", reinterpret_cast<PyCFunction>(native_eval), METH_VARARGS,
     "Evaluate JavaScript in the current page."},
    {"run", reinterpret_cast<PyCFunction>(native_run), METH_VARARGS,
     "Run the native event loop."},
    {nullptr, nullptr, 0, nullptr}};

PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "_native",
    "Native AppVerse bindings over webview/webview.",
    -1,
    methods,
};

}  // namespace

PyMODINIT_FUNC PyInit__native(void) {
  PyObject *mod = PyModule_Create(&module);
  if (!mod) {
    return nullptr;
  }

  PyModule_AddIntConstant(mod, "HINT_NONE", WEBVIEW_HINT_NONE);
  PyModule_AddIntConstant(mod, "HINT_MIN", WEBVIEW_HINT_MIN);
  PyModule_AddIntConstant(mod, "HINT_MAX", WEBVIEW_HINT_MAX);
  PyModule_AddIntConstant(mod, "HINT_FIXED", WEBVIEW_HINT_FIXED);

  return mod;
}
