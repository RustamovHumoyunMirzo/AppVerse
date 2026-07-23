#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "webview/webview.h"

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#endif

namespace {

constexpr const char *kCapsuleName = "appverse.WindowHandle";

struct BindingContext;

struct WindowHandle {
  std::unique_ptr<webview::webview> window;
  std::unordered_map<std::string, std::unique_ptr<BindingContext>> bindings;
};

struct BindingContext {
  WindowHandle *handle{};
  PyObject *callable{};

  ~BindingContext() {
    PyGILState_STATE gil = PyGILState_Ensure();
    Py_XDECREF(callable);
    PyGILState_Release(gil);
  }
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

void raise_runtime_error(const std::exception &e) {
  PyErr_SetString(PyExc_RuntimeError, e.what());
}

PyObject *none_on_success() {
  Py_RETURN_NONE;
}

void binding_callback(const std::string &id, const std::string &request, void *arg) {
  auto *context = static_cast<BindingContext *>(arg);
  if (!context || !context->handle || !context->handle->window || !context->callable) {
    return;
  }

  PyGILState_STATE gil = PyGILState_Ensure();
  PyObject *result = PyObject_CallFunction(context->callable, "ss", id.c_str(), request.c_str());

  int status = 0;
  std::string payload;

  if (!result) {
    PyErr_Print();
    status = 1;
    payload = "\"Python binding raised an exception\"";
  } else if (result == Py_None) {
    payload = "null";
  } else {
    PyObject *text = PyObject_Str(result);
    if (!text) {
      PyErr_Print();
      status = 1;
      payload = "\"Could not stringify Python binding result\"";
    } else {
      const char *utf8 = PyUnicode_AsUTF8(text);
      payload = utf8 ? utf8 : "null";
      Py_DECREF(text);
    }
    Py_DECREF(result);
  }

  try {
    context->handle->window->resolve(id, status, payload);
  } catch (const std::exception &e) {
    PyErr_SetString(PyExc_RuntimeError, e.what());
    PyErr_Print();
  }

  PyGILState_Release(gil);
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
        std::make_unique<webview::webview>(debug != 0, nullptr), {}};
    return PyCapsule_New(handle, kCapsuleName, capsule_destructor);
  } catch (const std::exception &e) {
    raise_runtime_error(e);
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

  handle->bindings.clear();
  handle->window.reset();
  return none_on_success();
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

  try {
    handle->window->set_title(title);
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
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

  try {
    handle->window->set_size(width, height, static_cast<webview_hint_t>(hint));
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
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

  try {
    handle->window->set_html(html);
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
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

  try {
    handle->window->navigate(url);
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
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

  try {
    handle->window->init(js);
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
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

  try {
    handle->window->eval(js);
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
}

PyObject *native_dispatch_eval(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *js = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &js)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    std::string script{js};
    handle->window->dispatch([handle, script] {
      if (handle->window) {
        handle->window->eval(script);
      }
    });
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
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

  try {
    Py_BEGIN_ALLOW_THREADS
    handle->window->run();
    Py_END_ALLOW_THREADS
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
}

PyObject *native_terminate(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    handle->window->terminate();
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
  return none_on_success();
}

PyObject *native_bind(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *name = nullptr;
  PyObject *callable = nullptr;

  if (!PyArg_ParseTuple(args, "OsO", &capsule, &name, &callable)) {
    return nullptr;
  }

  if (!PyCallable_Check(callable)) {
    PyErr_SetString(PyExc_TypeError, "binding callback must be callable");
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto context = std::make_unique<BindingContext>();
    context->handle = handle;
    Py_INCREF(callable);
    context->callable = callable;
    BindingContext *raw = context.get();

    auto existing = handle->bindings.find(name);
    if (existing != handle->bindings.end()) {
      handle->window->unbind(name);
      handle->bindings.erase(existing);
    }

    handle->window->bind(name, binding_callback, raw);
    handle->bindings[name] = std::move(context);
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }

  return none_on_success();
}

PyObject *native_unbind(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *name = nullptr;

  if (!PyArg_ParseTuple(args, "Os", &capsule, &name)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    handle->window->unbind(name);
    auto existing = handle->bindings.find(name);
    if (existing != handle->bindings.end()) {
      handle->bindings.erase(existing);
    }
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }

  return none_on_success();
}

PyObject *native_set_icon(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *path = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &path)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

#if defined(_WIN32)
  try {
    auto result = handle->window->window();
    result.ensure_ok();
    auto *hwnd = static_cast<HWND>(result.value());
    int length = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (length <= 0) {
      Py_RETURN_FALSE;
    }
    std::wstring wide_path(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path.data(), length);

    HANDLE icon = LoadImageW(nullptr, wide_path.c_str(), IMAGE_ICON, 0, 0,
                             LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!icon) {
      Py_RETURN_FALSE;
    }

    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    Py_RETURN_TRUE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
#else
  Py_RETURN_FALSE;
#endif
}

PyObject *native_set_frameless(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int frameless = 0;
  if (!PyArg_ParseTuple(args, "Op", &capsule, &frameless)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

#if defined(_WIN32)
  try {
    auto result = handle->window->window();
    result.ensure_ok();
    auto *hwnd = static_cast<HWND>(result.value());
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (frameless) {
      style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    } else {
      style |= (WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    }
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    Py_RETURN_TRUE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
#else
  Py_RETURN_FALSE;
#endif
}

PyObject *native_set_visible(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int visible = 0;
  if (!PyArg_ParseTuple(args, "Op", &capsule, &visible)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

#if defined(_WIN32)
  try {
    auto result = handle->window->window();
    result.ensure_ok();
    auto *hwnd = static_cast<HWND>(result.value());
    ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
    Py_RETURN_TRUE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
#elif defined(__APPLE__)
  try {
    auto result = handle->window->window();
    result.ensure_ok();
    auto window = static_cast<id>(result.value());
    if (visible) {
      using Fn = void (*)(id, SEL, id);
      reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("makeKeyAndOrderFront:"), nil);
    } else {
      using Fn = void (*)(id, SEL, id);
      reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("orderOut:"), nil);
    }
    Py_RETURN_TRUE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
#elif defined(__linux__)
  try {
    auto result = handle->window->window();
    result.ensure_ok();
    auto *window = static_cast<GtkWidget *>(result.value());
    gtk_widget_set_visible(window, visible != 0);
    if (visible && GTK_IS_WINDOW(window)) {
      gtk_window_present(GTK_WINDOW(window));
    }
    Py_RETURN_TRUE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
#else
  Py_RETURN_FALSE;
#endif
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
    {"dispatch_eval", reinterpret_cast<PyCFunction>(native_dispatch_eval), METH_VARARGS,
     "Dispatch JavaScript evaluation onto the native UI loop."},
    {"run", reinterpret_cast<PyCFunction>(native_run), METH_VARARGS,
     "Run the native event loop."},
    {"terminate", reinterpret_cast<PyCFunction>(native_terminate), METH_VARARGS,
     "Terminate the native event loop."},
    {"bind", reinterpret_cast<PyCFunction>(native_bind), METH_VARARGS,
     "Bind a JavaScript-callable Python callback."},
    {"unbind", reinterpret_cast<PyCFunction>(native_unbind), METH_VARARGS,
     "Remove a JavaScript binding."},
    {"set_icon", reinterpret_cast<PyCFunction>(native_set_icon), METH_VARARGS,
     "Set the native window icon."},
    {"set_frameless", reinterpret_cast<PyCFunction>(native_set_frameless), METH_VARARGS,
     "Toggle native window frame decorations."},
    {"set_visible", reinterpret_cast<PyCFunction>(native_set_visible), METH_VARARGS,
     "Toggle native window visibility."},
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
