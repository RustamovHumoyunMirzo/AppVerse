#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "webview/webview.h"

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#endif

namespace {

constexpr const char *kCapsuleName = "appverse.WindowHandle";
constexpr const char *kPreloadHtml =
    "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
    "html,body{margin:0;width:100%;height:100%;background:#101418;}"
    "</style></head><body></body></html>";

struct BindingContext;

struct WindowHandle {
  std::unique_ptr<webview::webview> window;
  std::unordered_map<std::string, std::unique_ptr<BindingContext>> bindings;
#if defined(_WIN32)
  bool fullscreen = false;
  WINDOWPLACEMENT previous_placement{};
  LONG_PTR previous_style = 0;
#endif
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

#if defined(__APPLE__)
struct CocoaPoint {
  double x;
  double y;
};

id cocoa_send_id(id receiver, const char *selector) {
  using Fn = id (*)(id, SEL);
  return reinterpret_cast<Fn>(objc_msgSend)(receiver, sel_registerName(selector));
}

id cocoa_class(const char *name) {
  return reinterpret_cast<id>(objc_getClass(name));
}

id cocoa_string(const char *utf8) {
  id ns_string = cocoa_class("NSString");
  id allocated = cocoa_send_id(ns_string, "alloc");
  using Fn = id (*)(id, SEL, const char *);
  return reinterpret_cast<Fn>(objc_msgSend)(allocated, sel_registerName("initWithUTF8String:"), utf8);
}

void cocoa_release(id object) {
  if (!object) {
    return;
  }
  using Fn = void (*)(id, SEL);
  reinterpret_cast<Fn>(objc_msgSend)(object, sel_registerName("release"));
}

bool cocoa_bool(id receiver, const char *selector) {
  using Fn = bool (*)(id, SEL);
  return reinterpret_cast<Fn>(objc_msgSend)(receiver, sel_registerName(selector));
}

unsigned long long cocoa_unsigned_long_long(id receiver, const char *selector) {
  using Fn = unsigned long long (*)(id, SEL);
  return reinterpret_cast<Fn>(objc_msgSend)(receiver, sel_registerName(selector));
}
#endif

bool apply_visibility(WindowHandle *handle, bool visible) {
#if defined(_WIN32)
  auto result = handle->window->window();
  result.ensure_ok();
  auto *hwnd = static_cast<HWND>(result.value());
  ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
  return true;
#elif defined(__APPLE__)
  auto result = handle->window->window();
  result.ensure_ok();
  auto window = static_cast<id>(result.value());
  using Fn = void (*)(id, SEL, id);
  if (visible) {
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("makeKeyAndOrderFront:"), nil);
  } else {
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("orderOut:"), nil);
  }
  return true;
#elif defined(__linux__)
  auto result = handle->window->window();
  result.ensure_ok();
  auto *window = static_cast<GtkWidget *>(result.value());
  gtk_widget_set_visible(window, visible);
  if (visible && GTK_IS_WINDOW(window)) {
    gtk_window_present(GTK_WINDOW(window));
  }
  return true;
#else
  return false;
#endif
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
  int visible = 1;
  static const char *keywords[] = {"debug", "visible", nullptr};

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|pp", const_cast<char **>(keywords),
                                   &debug, &visible)) {
    return nullptr;
  }

  try {
    auto window = std::make_unique<webview::webview>(debug != 0, nullptr);
    window->set_html(kPreloadHtml);
    auto handle = std::make_unique<WindowHandle>();
    handle->window = std::move(window);
    if (!visible) {
      apply_visibility(handle.get(), false);
    }
    return PyCapsule_New(handle.release(), kCapsuleName, capsule_destructor);
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

    HICON icon = static_cast<HICON>(
        LoadImageW(nullptr, wide_path.c_str(), IMAGE_ICON, 0, 0,
                   LR_LOADFROMFILE | LR_DEFAULTSIZE));
    if (!icon) {
      SHFILEINFOW info{};
      DWORD_PTR ok = SHGetFileInfoW(wide_path.c_str(), 0, &info, sizeof(info),
                                    SHGFI_ICON | SHGFI_LARGEICON);
      if (ok) {
        icon = info.hIcon;
      }
    }
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
#elif defined(__APPLE__)
  try {
    id path_string = cocoa_string(path);
    if (!path_string) {
      Py_RETURN_FALSE;
    }

    id image_class = cocoa_class("NSImage");
    id image = cocoa_send_id(image_class, "alloc");
    using InitFn = id (*)(id, SEL, id);
    image = reinterpret_cast<InitFn>(objc_msgSend)(
        image, sel_registerName("initWithContentsOfFile:"), path_string);
    cocoa_release(path_string);
    if (!image) {
      Py_RETURN_FALSE;
    }

    id app = cocoa_send_id(cocoa_class("NSApplication"), "sharedApplication");
    using SetImageFn = void (*)(id, SEL, id);
    reinterpret_cast<SetImageFn>(objc_msgSend)(
        app, sel_registerName("setApplicationIconImage:"), image);
    cocoa_release(image);
    Py_RETURN_TRUE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
#elif defined(__linux__) && GTK_MAJOR_VERSION < 4
  try {
    auto result = handle->window->window();
    result.ensure_ok();
    auto *window = static_cast<GtkWidget *>(result.value());
    if (!GTK_IS_WINDOW(window)) {
      Py_RETURN_FALSE;
    }

    GError *error = nullptr;
    gboolean ok = gtk_window_set_icon_from_file(GTK_WINDOW(window), path, &error);
    if (error) {
      g_error_free(error);
    }
    if (ok) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
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
      style &= ~(WS_CAPTION | WS_THICKFRAME);
      style |= (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
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
#elif defined(__APPLE__)
  try {
    auto result = handle->window->window();
    result.ensure_ok();
    auto window = static_cast<id>(result.value());
    constexpr unsigned long long titled = 1ULL << 0;
    constexpr unsigned long long closable = 1ULL << 1;
    constexpr unsigned long long miniaturizable = 1ULL << 2;
    constexpr unsigned long long resizable = 1ULL << 3;
    auto style = cocoa_unsigned_long_long(window, "styleMask");
    if (frameless) {
      style &= ~titled;
      style |= (closable | miniaturizable | resizable);
    } else {
      style |= (titled | closable | miniaturizable | resizable);
    }
    using Fn = void (*)(id, SEL, unsigned long long);
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("setStyleMask:"), style);
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
    if (GTK_IS_WINDOW(window)) {
      gtk_window_set_decorated(GTK_WINDOW(window), frameless == 0);
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
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

  try {
    if (apply_visibility(handle, visible != 0)) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_set_position(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int x = 0;
  int y = 0;
  if (!PyArg_ParseTuple(args, "Oii", &capsule, &x, &y)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    CocoaPoint point{static_cast<double>(x), static_cast<double>(y)};
    using Fn = void (*)(id, SEL, CocoaPoint);
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("setFrameOrigin:"), point);
    Py_RETURN_TRUE;
#elif defined(__linux__) && GTK_MAJOR_VERSION < 4
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      gtk_window_move(GTK_WINDOW(window), x, y);
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_minimize(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    ShowWindow(static_cast<HWND>(result.value()), SW_MINIMIZE);
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    using Fn = void (*)(id, SEL, id);
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("performMiniaturize:"), nil);
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
#if GTK_MAJOR_VERSION >= 4
      gtk_window_minimize(GTK_WINDOW(window));
#else
      gtk_window_iconify(GTK_WINDOW(window));
#endif
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_maximize(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    ShowWindow(static_cast<HWND>(result.value()), SW_MAXIMIZE);
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    using Fn = void (*)(id, SEL, id);
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("zoom:"), nil);
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      gtk_window_maximize(GTK_WINDOW(window));
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_restore(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    if (handle->fullscreen) {
      SetWindowLongPtrW(hwnd, GWL_STYLE, handle->previous_style);
      SetWindowPlacement(hwnd, &handle->previous_placement);
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
      handle->fullscreen = false;
    } else {
      ShowWindow(hwnd, SW_RESTORE);
    }
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    using Fn = void (*)(id, SEL, id);
    if (cocoa_bool(window, "isMiniaturized")) {
      reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("deminiaturize:"), nil);
    }
    if (cocoa_bool(window, "isZoomed")) {
      reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("zoom:"), nil);
    }
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      gtk_window_unmaximize(GTK_WINDOW(window));
      gtk_window_present(GTK_WINDOW(window));
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_toggle_maximize(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    if (handle->fullscreen || IsZoomed(hwnd)) {
      if (handle->fullscreen) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, handle->previous_style);
        SetWindowPlacement(hwnd, &handle->previous_placement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        handle->fullscreen = false;
      } else {
        ShowWindow(hwnd, SW_RESTORE);
      }
    } else {
      ShowWindow(hwnd, SW_MAXIMIZE);
    }
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    using Fn = void (*)(id, SEL, id);
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("zoom:"), nil);
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
#if GTK_MAJOR_VERSION >= 4
      if (gtk_window_is_maximized(GTK_WINDOW(window))) {
        gtk_window_unmaximize(GTK_WINDOW(window));
      } else {
        gtk_window_maximize(GTK_WINDOW(window));
      }
#else
      auto *gdk_window = gtk_widget_get_window(window);
      bool maximized = false;
      if (gdk_window) {
        maximized = (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
      }
      if (maximized) {
        gtk_window_unmaximize(GTK_WINDOW(window));
      } else {
        gtk_window_maximize(GTK_WINDOW(window));
      }
#endif
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_set_fullscreen(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int fullscreen = 0;
  if (!PyArg_ParseTuple(args, "Op", &capsule, &fullscreen)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    if (fullscreen && !handle->fullscreen) {
      handle->previous_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
      handle->previous_placement.length = sizeof(WINDOWPLACEMENT);
      GetWindowPlacement(hwnd, &handle->previous_placement);

      MONITORINFO monitor_info{};
      monitor_info.cbSize = sizeof(MONITORINFO);
      if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                           &monitor_info)) {
        Py_RETURN_FALSE;
      }

      SetWindowLongPtrW(hwnd, GWL_STYLE,
                        handle->previous_style & ~(WS_CAPTION | WS_THICKFRAME));
      SetWindowPos(hwnd, HWND_TOP,
                   monitor_info.rcMonitor.left,
                   monitor_info.rcMonitor.top,
                   monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                   monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
      handle->fullscreen = true;
    } else if (!fullscreen && handle->fullscreen) {
      SetWindowLongPtrW(hwnd, GWL_STYLE, handle->previous_style);
      SetWindowPlacement(hwnd, &handle->previous_placement);
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                       SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
      handle->fullscreen = false;
    }
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    auto mask = cocoa_unsigned_long_long(window, "styleMask");
    bool is_fullscreen = (mask & (1ULL << 14)) != 0;
    if (is_fullscreen != static_cast<bool>(fullscreen)) {
      using Fn = void (*)(id, SEL, id);
      reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("toggleFullScreen:"), nil);
    }
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      if (fullscreen) {
        gtk_window_fullscreen(GTK_WINDOW(window));
      } else {
        gtk_window_unfullscreen(GTK_WINDOW(window));
      }
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_start_drag(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    Class ns_application = objc_getClass("NSApplication");
    using SharedAppFn = id (*)(id, SEL);
    auto app = reinterpret_cast<SharedAppFn>(objc_msgSend)(
        reinterpret_cast<id>(ns_application), sel_registerName("sharedApplication"));
    using EventFn = id (*)(id, SEL);
    auto event = reinterpret_cast<EventFn>(objc_msgSend)(app, sel_registerName("currentEvent"));
    if (!event) {
      Py_RETURN_FALSE;
    }
    using DragFn = void (*)(id, SEL, id);
    reinterpret_cast<DragFn>(objc_msgSend)(window, sel_registerName("performWindowDragWithEvent:"), event);
    Py_RETURN_TRUE;
#elif defined(__linux__) && GTK_MAJOR_VERSION < 4
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      gtk_window_begin_move_drag(GTK_WINDOW(window), 1, 0, 0, gtk_get_current_event_time());
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_show_window_menu(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int x = 0;
  int y = 0;
  if (!PyArg_ParseTuple(args, "Oii", &capsule, &x, &y)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) {
      Py_RETURN_FALSE;
    }

    const bool minimized = IsIconic(hwnd) != FALSE;
    const bool maximized = IsZoomed(hwnd) != FALSE;
    EnableMenuItem(menu, SC_RESTORE,
                   MF_BYCOMMAND | ((minimized || maximized) ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, SC_MAXIMIZE,
                   MF_BYCOMMAND | (maximized ? MF_GRAYED : MF_ENABLED));
    EnableMenuItem(menu, SC_MINIMIZE,
                   MF_BYCOMMAND | (minimized ? MF_GRAYED : MF_ENABLED));
    DrawMenuBar(hwnd);

    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
      cursor.x = x;
      cursor.y = y;
    }

    SetForegroundWindow(hwnd);
    UINT flags = TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN;
    int command =
        TrackPopupMenu(menu, flags, cursor.x, cursor.y, 0, hwnd, nullptr);
    if (command != 0) {
      PostMessageW(hwnd, WM_SYSCOMMAND, static_cast<WPARAM>(command), 0);
    }
    PostMessageW(hwnd, WM_NULL, 0, 0);
    Py_RETURN_TRUE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
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
    {"set_position", reinterpret_cast<PyCFunction>(native_set_position), METH_VARARGS,
     "Set native window position."},
    {"minimize", reinterpret_cast<PyCFunction>(native_minimize), METH_VARARGS,
     "Minimize the native window."},
    {"maximize", reinterpret_cast<PyCFunction>(native_maximize), METH_VARARGS,
     "Maximize the native window."},
    {"restore", reinterpret_cast<PyCFunction>(native_restore), METH_VARARGS,
     "Restore the native window."},
    {"toggle_maximize", reinterpret_cast<PyCFunction>(native_toggle_maximize), METH_VARARGS,
     "Toggle native window maximized state."},
    {"set_fullscreen", reinterpret_cast<PyCFunction>(native_set_fullscreen), METH_VARARGS,
     "Toggle native fullscreen mode."},
    {"start_drag", reinterpret_cast<PyCFunction>(native_start_drag), METH_VARARGS,
     "Start a native window drag operation."},
    {"show_window_menu", reinterpret_cast<PyCFunction>(native_show_window_menu), METH_VARARGS,
     "Show the native window control context menu."},
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
