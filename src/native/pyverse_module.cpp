#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "webview/webview.h"

#include <cctype>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#include <dwmapi.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <objc/message.h>
#include <objc/objc.h>
#include <objc/runtime.h>
#elif defined(__linux__)
#include <gtk/gtk.h>
#endif

#if defined(_WIN32) && !defined(DWMWA_SYSTEMBACKDROP_TYPE)
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#if defined(_WIN32) && !defined(DWMWA_BORDER_COLOR)
#define DWMWA_BORDER_COLOR 34
#endif
#if defined(_WIN32) && !defined(DWMWA_CAPTION_COLOR)
#define DWMWA_CAPTION_COLOR 35
#endif
#if defined(_WIN32) && !defined(DWMWA_TEXT_COLOR)
#define DWMWA_TEXT_COLOR 36
#endif
#if defined(_WIN32) && !defined(DWMWA_NCRENDERING_POLICY)
#define DWMWA_NCRENDERING_POLICY 2
#endif
#if defined(_WIN32) && !defined(DWMWA_WINDOW_CORNER_PREFERENCE)
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

namespace {

constexpr const char *kCapsuleName = "appverse.WindowHandle";
constexpr const char *kPreloadHtml =
    "<!doctype html><html><head><meta charset=\"utf-8\"><style>"
    "html,body{margin:0;width:100%;height:100%;background:#101418;}"
    "</style></head><body></body></html>";

struct MenuCallbackContext;
std::unordered_map<int, MenuCallbackContext *> g_menu_callbacks;

enum class BackdropEffect {
  EffectNone,
  EffectAcrylic,
  EffectMica,
  EffectGlass,
};

struct RgbaColor {
  unsigned char red = 0;
  unsigned char green = 0;
  unsigned char blue = 0;
  unsigned char alpha = 255;
};

struct BindingContext;

struct WindowHandle {
  std::unique_ptr<webview::webview> window;
  std::unordered_map<std::string, std::unique_ptr<BindingContext>> bindings;
  std::unordered_map<int, std::unique_ptr<MenuCallbackContext>> menu_callbacks;
  std::unordered_map<std::string, void *> menu_containers;
  bool fullscreenable = true;
  bool devtools_enabled = false;
  bool hardware_acceleration_enabled = true;
  bool shadow_enabled = true;
  bool resizable = true;
  bool movable = true;
  bool always_on_top = false;
  bool skip_taskbar = false;
  bool closable = true;
  bool minimizable = true;
  bool maximizable = true;
  int rounded_corner_radius = 8;
  std::string shadow_style = "system";
#if defined(_WIN32)
  bool fullscreen = false;
  HBRUSH background_brush = nullptr;
  HMENU menu_bar = nullptr;
  WNDPROC previous_wndproc = nullptr;
  WINDOWPLACEMENT previous_placement{};
  LONG_PTR previous_style = 0;
#endif

  ~WindowHandle() {
#if defined(_WIN32)
    if (background_brush) {
      DeleteObject(background_brush);
    }
#endif
  }
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

struct MenuCallbackContext {
  WindowHandle *handle{};
  int item_id = 0;
  PyObject *callable{};

  ~MenuCallbackContext() {
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

std::string menu_key(const std::vector<std::string> &path) {
  std::string key;
  for (const auto &part : path) {
    if (!key.empty()) {
      key += "/";
    }
    key += part;
  }
  return key;
}

bool parse_menu_path(PyObject *path_object, std::vector<std::string> *path) {
  if (!path) {
    return false;
  }
  path->clear();
  if (!PySequence_Check(path_object)) {
    PyErr_SetString(PyExc_TypeError, "menu path must be a sequence of strings");
    return false;
  }
  Py_ssize_t size = PySequence_Size(path_object);
  if (size < 0) {
    return false;
  }
  for (Py_ssize_t i = 0; i < size; ++i) {
    PyObject *item = PySequence_GetItem(path_object, i);
    if (!item) {
      return false;
    }
    if (!PyUnicode_Check(item)) {
      Py_DECREF(item);
      PyErr_SetString(PyExc_TypeError, "menu path items must be strings");
      return false;
    }
    const char *text = PyUnicode_AsUTF8(item);
    if (!text) {
      Py_DECREF(item);
      return false;
    }
    path->emplace_back(text);
    Py_DECREF(item);
  }
  return true;
}

void invoke_menu_callback(int item_id) {
  MenuCallbackContext *context = nullptr;
  auto found = g_menu_callbacks.find(item_id);
  if (found == g_menu_callbacks.end()) {
    return;
  }
  context = found->second;
  if (!context || !context->callable) {
    return;
  }
  PyGILState_STATE gil = PyGILState_Ensure();
  PyObject *result = PyObject_CallFunction(context->callable, "i", item_id);
  if (!result) {
    PyErr_Print();
  } else {
    Py_DECREF(result);
  }
  PyGILState_Release(gil);
}

#if defined(_WIN32)
std::wstring utf8_to_wide(const std::string &value) {
  if (value.empty()) {
    return L"";
  }
  int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  if (size <= 0) {
    return L"";
  }
  std::wstring result(static_cast<size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
  return result;
}

LRESULT CALLBACK appverse_menu_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto *handle = reinterpret_cast<WindowHandle *>(
      GetPropW(hwnd, L"AppVerseWindowHandle"));
  if (msg == WM_COMMAND && handle) {
    int item_id = LOWORD(wp);
    if (g_menu_callbacks.find(item_id) != g_menu_callbacks.end()) {
      invoke_menu_callback(item_id);
      return 0;
    }
  }
  if (handle && handle->previous_wndproc) {
    return CallWindowProcW(handle->previous_wndproc, hwnd, msg, wp, lp);
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

HMENU ensure_win32_menu(WindowHandle *handle, HWND hwnd,
                        const std::vector<std::string> &path) {
  if (!handle->menu_bar) {
    handle->menu_bar = CreateMenu();
    SetMenu(hwnd, handle->menu_bar);
    SetPropW(hwnd, L"AppVerseWindowHandle", handle);
    handle->previous_wndproc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(appverse_menu_wndproc)));
  }

  HMENU current = handle->menu_bar;
  std::vector<std::string> key_parts;
  for (const auto &part : path) {
    key_parts.push_back(part);
    std::string key = menu_key(key_parts);
    auto found = handle->menu_containers.find(key);
    if (found != handle->menu_containers.end()) {
      current = static_cast<HMENU>(found->second);
      continue;
    }
    HMENU submenu = CreatePopupMenu();
    std::wstring label = utf8_to_wide(part);
    AppendMenuW(current, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu),
                label.c_str());
    handle->menu_containers[key] = submenu;
    current = submenu;
  }
  return current;
}
#endif


bool parse_backdrop_effect(const char *name, BackdropEffect *effect) {
  if (!name || !effect) {
    return false;
  }
  std::string value{name};
  for (auto &ch : value) {
    if (ch >= 'A' && ch <= 'Z') {
      ch = static_cast<char>(ch - 'A' + 'a');
    }
  }
  if (value == "none" || value == "off" || value == "solid") {
    *effect = BackdropEffect::EffectNone;
    return true;
  }
  if (value == "acrylic") {
    *effect = BackdropEffect::EffectAcrylic;
    return true;
  }
  if (value == "mica") {
    *effect = BackdropEffect::EffectMica;
    return true;
  }
  if (value == "glass" || value == "tabbed") {
    *effect = BackdropEffect::EffectGlass;
    return true;
  }
  return false;
}

std::string normalized_text(const char *text) {
  std::string value{text ? text : ""};
  size_t start = 0;
  while (start < value.size() &&
         std::isspace(static_cast<unsigned char>(value[start])) != 0) {
    ++start;
  }
  size_t end = value.size();
  while (end > start &&
         std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  value = value.substr(start, end - start);
  for (auto &ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return -1;
}

bool parse_hex_byte(const std::string &value, size_t offset, unsigned char *out) {
  if (!out || offset + 1 >= value.size()) {
    return false;
  }
  int high = hex_value(value[offset]);
  int low = hex_value(value[offset + 1]);
  if (high < 0 || low < 0) {
    return false;
  }
  *out = static_cast<unsigned char>((high << 4) | low);
  return true;
}

bool parse_int_component(const std::string &value, size_t *offset, int *out) {
  if (!offset || !out) {
    return false;
  }
  while (*offset < value.size() &&
         std::isspace(static_cast<unsigned char>(value[*offset])) != 0) {
    ++(*offset);
  }
  size_t start = *offset;
  while (*offset < value.size() &&
         std::isdigit(static_cast<unsigned char>(value[*offset])) != 0) {
    ++(*offset);
  }
  if (start == *offset) {
    return false;
  }
  int number = std::stoi(value.substr(start, *offset - start));
  if (number < 0 || number > 255) {
    return false;
  }
  *out = number;
  return true;
}

bool parse_alpha_component(const std::string &value, size_t *offset, unsigned char *out) {
  if (!offset || !out) {
    return false;
  }
  while (*offset < value.size() &&
         std::isspace(static_cast<unsigned char>(value[*offset])) != 0) {
    ++(*offset);
  }
  size_t start = *offset;
  while (*offset < value.size() &&
         (std::isdigit(static_cast<unsigned char>(value[*offset])) != 0 ||
          value[*offset] == '.')) {
    ++(*offset);
  }
  if (start == *offset) {
    return false;
  }
  double alpha = std::stod(value.substr(start, *offset - start));
  if (alpha >= 0.0 && alpha <= 1.0) {
    *out = static_cast<unsigned char>(alpha * 255.0 + 0.5);
    return true;
  }
  if (alpha >= 0.0 && alpha <= 255.0) {
    *out = static_cast<unsigned char>(alpha + 0.5);
    return true;
  }
  return false;
}

bool consume_comma(const std::string &value, size_t *offset) {
  if (!offset) {
    return false;
  }
  while (*offset < value.size() &&
         std::isspace(static_cast<unsigned char>(value[*offset])) != 0) {
    ++(*offset);
  }
  if (*offset >= value.size() || value[*offset] != ',') {
    return false;
  }
  ++(*offset);
  return true;
}

bool parse_background_color(const char *color_text, RgbaColor *color) {
  if (!color) {
    return false;
  }
  std::string value = normalized_text(color_text);
  if (value == "transparent" || value == "none") {
    *color = RgbaColor{0, 0, 0, 0};
    return true;
  }
  if (value.rfind("#", 0) == 0) {
    if (value.size() == 7) {
      return parse_hex_byte(value, 1, &color->red) &&
             parse_hex_byte(value, 3, &color->green) &&
             parse_hex_byte(value, 5, &color->blue);
    }
    if (value.size() == 9) {
      return parse_hex_byte(value, 1, &color->red) &&
             parse_hex_byte(value, 3, &color->green) &&
             parse_hex_byte(value, 5, &color->blue) &&
             parse_hex_byte(value, 7, &color->alpha);
    }
    return false;
  }
  bool has_alpha = value.rfind("rgba(", 0) == 0;
  bool has_rgb = value.rfind("rgb(", 0) == 0;
  if (has_alpha || has_rgb) {
    size_t offset = has_alpha ? 5 : 4;
    int red = 0;
    int green = 0;
    int blue = 0;
    unsigned char alpha = 255;
    if (!parse_int_component(value, &offset, &red) ||
        !consume_comma(value, &offset) ||
        !parse_int_component(value, &offset, &green) ||
        !consume_comma(value, &offset) ||
        !parse_int_component(value, &offset, &blue)) {
      return false;
    }
    if (has_alpha) {
      if (!consume_comma(value, &offset) ||
          !parse_alpha_component(value, &offset, &alpha)) {
        return false;
      }
    }
    while (offset < value.size() &&
           std::isspace(static_cast<unsigned char>(value[offset])) != 0) {
      ++offset;
    }
    if (offset >= value.size() || value[offset] != ')') {
      return false;
    }
    *color = RgbaColor{static_cast<unsigned char>(red),
                       static_cast<unsigned char>(green),
                       static_cast<unsigned char>(blue),
                       alpha};
    return true;
  }
  return false;
}

bool parse_optional_color(const char *color_text, RgbaColor *color, bool *has_color) {
  if (!color || !has_color) {
    return false;
  }
  *has_color = false;
  if (!color_text || color_text[0] == '\0') {
    return true;
  }
  *has_color = true;
  return parse_background_color(color_text, color);
}

#if defined(__APPLE__)
struct CocoaPoint {
  double x;
  double y;
};

struct CocoaSize {
  double width;
  double height;
};

struct CocoaRect {
  CocoaPoint origin;
  CocoaSize size;
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

bool apply_devtools_enabled(WindowHandle *handle, bool enabled) {
  if (!handle || !handle->window) {
    return false;
  }
  handle->devtools_enabled = enabled;

  auto result = handle->window->browser_controller();
  result.ensure_ok();
#if defined(_WIN32)
  auto *controller = static_cast<ICoreWebView2Controller *>(result.value());
  if (!controller) {
    return false;
  }

  ICoreWebView2 *webview = nullptr;
  HRESULT hr = controller->get_CoreWebView2(&webview);
  if (FAILED(hr) || !webview) {
    return false;
  }

  ICoreWebView2Settings *settings = nullptr;
  hr = webview->get_Settings(&settings);
  webview->Release();
  if (FAILED(hr) || !settings) {
    return false;
  }

  bool applied = SUCCEEDED(settings->put_AreDevToolsEnabled(enabled ? TRUE : FALSE));

  ICoreWebView2Settings3 *settings3 = nullptr;
  hr = settings->QueryInterface(IID_ICoreWebView2Settings3,
                                reinterpret_cast<void **>(&settings3));
  if (SUCCEEDED(hr) && settings3) {
    applied = SUCCEEDED(settings3->put_AreBrowserAcceleratorKeysEnabled(
                  enabled ? TRUE : FALSE)) &&
              applied;
    settings3->Release();
  }

  settings->Release();
  return applied;
#elif defined(__APPLE__)
  auto webview = static_cast<id>(result.value());
  id config = cocoa_send_id(webview, "configuration");
  id preferences = cocoa_send_id(config, "preferences");
  id number = nullptr;
  using NumberFn = id (*)(id, SEL, bool);
  number = reinterpret_cast<NumberFn>(objc_msgSend)(
      cocoa_class("NSNumber"), sel_registerName("numberWithBool:"), enabled);
  id key = cocoa_string("developerExtrasEnabled");
  using SetValueFn = void (*)(id, SEL, id, id);
  reinterpret_cast<SetValueFn>(objc_msgSend)(
      preferences, sel_registerName("setValue:forKey:"), number, key);
  cocoa_release(key);
  return true;
#elif defined(__linux__)
  auto *webview = static_cast<WebKitWebView *>(result.value());
  if (!WEBKIT_IS_WEB_VIEW(webview)) {
    return false;
  }
  WebKitSettings *settings = webkit_web_view_get_settings(webview);
  webkit_settings_set_enable_developer_extras(settings, enabled);
  webkit_settings_set_enable_write_console_messages_to_stdout(settings, enabled);
  return true;
#else
  return false;
#endif
}

bool set_gobject_bool_property(void *object, const char *name, bool enabled) {
#if defined(__linux__)
  if (!object || !name) {
    return false;
  }
  auto *gobject = G_OBJECT(object);
  GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(gobject), name);
  if (!property) {
    return false;
  }
  g_object_set(gobject, name, enabled ? TRUE : FALSE, nullptr);
  return true;
#else
  (void)object;
  (void)name;
  (void)enabled;
  return false;
#endif
}

#if defined(_WIN32)
bool configure_webview2_gpu_hint(bool enabled) {
  DWORD existing_size =
      GetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", nullptr, 0);
  if (existing_size > 0) {
    return true;
  }

  const wchar_t *args = enabled
                            ? L"--enable-gpu-rasterization --enable-zero-copy "
                              L"--ignore-gpu-blocklist"
                            : L"--disable-gpu --disable-software-rasterizer";
  return SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", args) != FALSE;
}
#endif

bool apply_hardware_acceleration_enabled(WindowHandle *handle, bool enabled) {
  if (!handle || !handle->window) {
    return false;
  }
  handle->hardware_acceleration_enabled = enabled;

#if defined(_WIN32)
  return enabled;
#elif defined(__APPLE__)
  return enabled;
#elif defined(__linux__)
  auto result = handle->window->browser_controller();
  result.ensure_ok();
  auto *webview = static_cast<WebKitWebView *>(result.value());
  if (!WEBKIT_IS_WEB_VIEW(webview)) {
    return false;
  }

  bool applied = false;
  WebKitSettings *settings = webkit_web_view_get_settings(webview);
  applied = set_gobject_bool_property(settings, "enable-webgl", enabled) || applied;
  applied = set_gobject_bool_property(settings, "enable-accelerated-2d-canvas", enabled) ||
            applied;
  applied = set_gobject_bool_property(settings, "enable-webaudio", enabled) || applied;
  applied = set_gobject_bool_property(settings, "enable-mediasource", enabled) || applied;
  return applied;
#else
  return false;
#endif
}

bool set_window_capability_state(WindowHandle *handle, const std::string &name,
                                 bool enabled) {
  if (name == "resizable") {
    handle->resizable = enabled;
  } else if (name == "movable") {
    handle->movable = enabled;
  } else if (name == "always_on_top" || name == "always-on-top" ||
             name == "alwaysontop") {
    handle->always_on_top = enabled;
  } else if (name == "skip_taskbar" || name == "skip-taskbar" ||
             name == "hidden_taskbar" || name == "hidden-taskbar") {
    handle->skip_taskbar = enabled;
  } else if (name == "closable") {
    handle->closable = enabled;
  } else if (name == "minimizable" || name == "minimizeable") {
    handle->minimizable = enabled;
  } else if (name == "maximizable" || name == "maximizeable") {
    handle->maximizable = enabled;
  } else {
    return false;
  }
  return true;
}

bool get_window_capability_state(WindowHandle *handle, const std::string &name,
                                 bool *enabled) {
  if (!enabled) {
    return false;
  }
  if (name == "resizable") {
    *enabled = handle->resizable;
  } else if (name == "movable") {
    *enabled = handle->movable;
  } else if (name == "always_on_top" || name == "always-on-top" ||
             name == "alwaysontop") {
    *enabled = handle->always_on_top;
  } else if (name == "skip_taskbar" || name == "skip-taskbar" ||
             name == "hidden_taskbar" || name == "hidden-taskbar") {
    *enabled = handle->skip_taskbar;
  } else if (name == "closable") {
    *enabled = handle->closable;
  } else if (name == "minimizable" || name == "minimizeable") {
    *enabled = handle->minimizable;
  } else if (name == "maximizable" || name == "maximizeable") {
    *enabled = handle->maximizable;
  } else {
    return false;
  }
  return true;
}

bool apply_window_capability(WindowHandle *handle, const std::string &name,
                             bool enabled) {
  if (!handle || !handle->window ||
      !set_window_capability_state(handle, name, enabled)) {
    return false;
  }

  auto result = handle->window->window();
  result.ensure_ok();
#if defined(_WIN32)
  auto *hwnd = static_cast<HWND>(result.value());
  LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
  LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

  if (name == "resizable") {
    if (enabled) {
      style |= WS_THICKFRAME;
    } else {
      style &= ~WS_THICKFRAME;
    }
  } else if (name == "minimizable" || name == "minimizeable") {
    if (enabled) {
      style |= WS_MINIMIZEBOX;
    } else {
      style &= ~WS_MINIMIZEBOX;
    }
  } else if (name == "maximizable" || name == "maximizeable") {
    if (enabled) {
      style |= WS_MAXIMIZEBOX;
    } else {
      style &= ~WS_MAXIMIZEBOX;
    }
  } else if (name == "closable") {
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (menu) {
      EnableMenuItem(menu, SC_CLOSE,
                     MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
      DrawMenuBar(hwnd);
    }
  } else if (name == "always_on_top" || name == "always-on-top" ||
             name == "alwaysontop") {
    SetWindowPos(hwnd, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE);
    return true;
  } else if (name == "skip_taskbar" || name == "skip-taskbar" ||
             name == "hidden_taskbar" || name == "hidden-taskbar") {
    if (enabled) {
      ex_style |= WS_EX_TOOLWINDOW;
      ex_style &= ~WS_EX_APPWINDOW;
    } else {
      ex_style &= ~WS_EX_TOOLWINDOW;
      ex_style |= WS_EX_APPWINDOW;
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex_style);
  } else if (name == "movable") {
    return true;
  }

  SetWindowLongPtrW(hwnd, GWL_STYLE, style);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
  return true;
#elif defined(__APPLE__)
  auto window = static_cast<id>(result.value());
  constexpr unsigned long long closable = 1ULL << 1;
  constexpr unsigned long long miniaturizable = 1ULL << 2;
  constexpr unsigned long long resizable = 1ULL << 3;
  auto style = cocoa_unsigned_long_long(window, "styleMask");
  bool style_changed = false;

  if (name == "resizable") {
    style = enabled ? (style | resizable) : (style & ~resizable);
    style_changed = true;
  } else if (name == "closable") {
    style = enabled ? (style | closable) : (style & ~closable);
    style_changed = true;
  } else if (name == "minimizable" || name == "minimizeable") {
    style = enabled ? (style | miniaturizable) : (style & ~miniaturizable);
    style_changed = true;
  } else if (name == "maximizable" || name == "maximizeable") {
    using ButtonFn = id (*)(id, SEL, unsigned long long);
    using EnabledFn = void (*)(id, SEL, bool);
    id button = reinterpret_cast<ButtonFn>(objc_msgSend)(
        window, sel_registerName("standardWindowButton:"), 2ULL);
    if (button) {
      reinterpret_cast<EnabledFn>(objc_msgSend)(
          button, sel_registerName("setEnabled:"), enabled);
    }
  } else if (name == "movable") {
    using BoolFn = void (*)(id, SEL, bool);
    reinterpret_cast<BoolFn>(objc_msgSend)(
        window, sel_registerName("setMovable:"), enabled);
  } else if (name == "always_on_top" || name == "always-on-top" ||
             name == "alwaysontop") {
    using LevelFn = void (*)(id, SEL, long);
    reinterpret_cast<LevelFn>(objc_msgSend)(
        window, sel_registerName("setLevel:"), enabled ? 3L : 0L);
  } else if (name == "skip_taskbar" || name == "skip-taskbar" ||
             name == "hidden_taskbar" || name == "hidden-taskbar") {
    auto behavior = cocoa_unsigned_long_long(window, "collectionBehavior");
    constexpr unsigned long long transient_behavior = 1ULL << 3;
    using BehaviorFn = void (*)(id, SEL, unsigned long long);
    reinterpret_cast<BehaviorFn>(objc_msgSend)(
        window, sel_registerName("setCollectionBehavior:"),
        enabled ? (behavior | transient_behavior)
                : (behavior & ~transient_behavior));
  }

  if (style_changed) {
    using StyleFn = void (*)(id, SEL, unsigned long long);
    reinterpret_cast<StyleFn>(objc_msgSend)(
        window, sel_registerName("setStyleMask:"), style);
  }
  return true;
#elif defined(__linux__)
  auto *window = static_cast<GtkWidget *>(result.value());
  if (!GTK_IS_WINDOW(window)) {
    return false;
  }
  if (name == "resizable") {
    gtk_window_set_resizable(GTK_WINDOW(window), enabled);
    return true;
  }
#if GTK_MAJOR_VERSION < 4
  if (name == "always_on_top" || name == "always-on-top" ||
      name == "alwaysontop") {
    gtk_window_set_keep_above(GTK_WINDOW(window), enabled);
    return true;
  }
  if (name == "skip_taskbar" || name == "skip-taskbar" ||
      name == "hidden_taskbar" || name == "hidden-taskbar") {
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), enabled);
    return true;
  }
  if (name == "closable") {
    gtk_window_set_deletable(GTK_WINDOW(window), enabled);
    return true;
  }
#endif
  return true;
#else
  return true;
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
  int hardware_acceleration = 1;
  static const char *keywords[] = {"debug", "visible", "hardware_acceleration", nullptr};

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|ppp", const_cast<char **>(keywords),
                                   &debug, &visible, &hardware_acceleration)) {
    return nullptr;
  }

  try {
#if defined(_WIN32)
    configure_webview2_gpu_hint(hardware_acceleration != 0);
#endif
    auto window = std::make_unique<webview::webview>(debug != 0, nullptr);
    window->set_html(kPreloadHtml);
    auto handle = std::make_unique<WindowHandle>();
    handle->window = std::move(window);
    apply_devtools_enabled(handle.get(), debug != 0);
    apply_hardware_acceleration_enabled(handle.get(), hardware_acceleration != 0);
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

  for (const auto &entry : handle->menu_callbacks) {
    g_menu_callbacks.erase(entry.first);
  }
  handle->menu_callbacks.clear();
  handle->bindings.clear();
  handle->window.reset();
  return none_on_success();
}

PyObject *native_set_devtools_enabled(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int enabled = 0;
  if (!PyArg_ParseTuple(args, "Op", &capsule, &enabled)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    if (apply_devtools_enabled(handle, enabled != 0)) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_is_devtools_enabled(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  if (handle->devtools_enabled) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

PyObject *native_set_hardware_acceleration_enabled(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int enabled = 1;
  if (!PyArg_ParseTuple(args, "Op", &capsule, &enabled)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    if (apply_hardware_acceleration_enabled(handle, enabled != 0)) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_is_hardware_acceleration_enabled(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  if (handle->hardware_acceleration_enabled) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
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

PyObject *native_add_menu_item(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  PyObject *path_object = nullptr;
  const char *label_text = nullptr;
  int item_id = 0;
  int enabled = 1;
  int separator = 0;
  PyObject *callable = Py_None;

  if (!PyArg_ParseTuple(args, "OOsippO", &capsule, &path_object, &label_text,
                        &item_id, &enabled, &separator, &callable)) {
    return nullptr;
  }

  std::vector<std::string> path;
  if (!parse_menu_path(path_object, &path)) {
    return nullptr;
  }

  if (callable != Py_None && !PyCallable_Check(callable)) {
    PyErr_SetString(PyExc_TypeError, "menu callback must be callable or None");
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  if (callable != Py_None && item_id <= 0) {
    PyErr_SetString(PyExc_ValueError, "clickable menu items need a positive item id");
    return nullptr;
  }

  if (callable != Py_None) {
    auto context = std::make_unique<MenuCallbackContext>();
    context->handle = handle;
    context->item_id = item_id;
    Py_INCREF(callable);
    context->callable = callable;
    g_menu_callbacks[item_id] = context.get();
    handle->menu_callbacks[item_id] = std::move(context);
  }

  std::string label{label_text ? label_text : ""};

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    HMENU parent = ensure_win32_menu(handle, hwnd, path);
    if (separator) {
      AppendMenuW(parent, MF_SEPARATOR, 0, nullptr);
    } else if (callable == Py_None) {
      std::vector<std::string> submenu_path = path;
      submenu_path.push_back(label);
      std::string key = menu_key(submenu_path);
      HMENU submenu = CreatePopupMenu();
      std::wstring wide_label = utf8_to_wide(label);
      AppendMenuW(parent, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu),
                  wide_label.c_str());
      handle->menu_containers[key] = submenu;
    } else {
      std::wstring wide_label = utf8_to_wide(label);
      AppendMenuW(parent, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED),
                  static_cast<UINT_PTR>(item_id), wide_label.c_str());
    }
    DrawMenuBar(hwnd);
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    id app = cocoa_send_id(cocoa_class("NSApplication"), "sharedApplication");
    id main_menu = cocoa_send_id(app, "mainMenu");
    if (!main_menu) {
      main_menu = cocoa_send_id(cocoa_class("NSMenu"), "alloc");
      using InitFn = id (*)(id, SEL);
      main_menu = reinterpret_cast<InitFn>(objc_msgSend)(
          main_menu, sel_registerName("init"));
      using SetMainMenuFn = void (*)(id, SEL, id);
      reinterpret_cast<SetMainMenuFn>(objc_msgSend)(
          app, sel_registerName("setMainMenu:"), main_menu);
    }

    auto ensure_menu = [&](const std::vector<std::string> &menu_path) -> id {
      id parent = main_menu;
      std::vector<std::string> key_parts;
      for (const auto &part : menu_path) {
        key_parts.push_back(part);
        std::string key = menu_key(key_parts);
        auto found = handle->menu_containers.find(key);
        if (found != handle->menu_containers.end()) {
          parent = static_cast<id>(found->second);
          continue;
        }
        id title = cocoa_string(part.c_str());
        id empty = cocoa_string("");
        using ItemInitFn = id (*)(id, SEL, id, SEL, id);
        id item = cocoa_send_id(cocoa_class("NSMenuItem"), "alloc");
        item = reinterpret_cast<ItemInitFn>(objc_msgSend)(
            item, sel_registerName("initWithTitle:action:keyEquivalent:"),
            title, nil, empty);
        id submenu = cocoa_send_id(cocoa_class("NSMenu"), "alloc");
        using MenuInitFn = id (*)(id, SEL, id);
        submenu = reinterpret_cast<MenuInitFn>(objc_msgSend)(
            submenu, sel_registerName("initWithTitle:"), title);
        using SetSubmenuFn = void (*)(id, SEL, id);
        reinterpret_cast<SetSubmenuFn>(objc_msgSend)(
            item, sel_registerName("setSubmenu:"), submenu);
        using AddItemFn = void (*)(id, SEL, id);
        reinterpret_cast<AddItemFn>(objc_msgSend)(
            parent, sel_registerName("addItem:"), item);
        handle->menu_containers[key] = submenu;
        parent = submenu;
        cocoa_release(title);
        cocoa_release(empty);
      }
      return parent;
    };

    static id menu_target = nil;
    if (!menu_target) {
      Class cls = objc_getClass("AppVerseMenuTarget");
      if (!cls) {
        cls = objc_allocateClassPair(cocoa_class("NSObject"),
                                     "AppVerseMenuTarget", 0);
        auto action = +[](id, SEL, id sender) {
          using TagFn = long (*)(id, SEL);
          long tag = reinterpret_cast<TagFn>(objc_msgSend)(
              sender, sel_registerName("tag"));
          invoke_menu_callback(static_cast<int>(tag));
        };
        class_addMethod(cls, sel_registerName("appverseMenuAction:"),
                        reinterpret_cast<IMP>(action), "v@:@");
        objc_registerClassPair(cls);
      }
      menu_target = cocoa_send_id(reinterpret_cast<id>(cls), "new");
    }

    id parent = ensure_menu(path);
    if (separator) {
      id separator_item = cocoa_send_id(cocoa_class("NSMenuItem"), "separatorItem");
      using AddItemFn = void (*)(id, SEL, id);
      reinterpret_cast<AddItemFn>(objc_msgSend)(
          parent, sel_registerName("addItem:"), separator_item);
    } else if (callable == Py_None) {
      std::vector<std::string> submenu_path = path;
      submenu_path.push_back(label);
      ensure_menu(submenu_path);
    } else {
      id title = cocoa_string(label.c_str());
      id empty = cocoa_string("");
      using ItemInitFn = id (*)(id, SEL, id, SEL, id);
      id item = cocoa_send_id(cocoa_class("NSMenuItem"), "alloc");
      item = reinterpret_cast<ItemInitFn>(objc_msgSend)(
          item, sel_registerName("initWithTitle:action:keyEquivalent:"), title,
          sel_registerName("appverseMenuAction:"), empty);
      using TargetFn = void (*)(id, SEL, id);
      reinterpret_cast<TargetFn>(objc_msgSend)(
          item, sel_registerName("setTarget:"), menu_target);
      using TagFn = void (*)(id, SEL, long);
      reinterpret_cast<TagFn>(objc_msgSend)(
          item, sel_registerName("setTag:"), static_cast<long>(item_id));
      using EnabledFn = void (*)(id, SEL, bool);
      reinterpret_cast<EnabledFn>(objc_msgSend)(
          item, sel_registerName("setEnabled:"), enabled != 0);
      using AddItemFn = void (*)(id, SEL, id);
      reinterpret_cast<AddItemFn>(objc_msgSend)(
          parent, sel_registerName("addItem:"), item);
      cocoa_release(title);
      cocoa_release(empty);
    }
    Py_RETURN_TRUE;
#elif defined(__linux__) && GTK_MAJOR_VERSION < 4
    auto *window = static_cast<GtkWidget *>(result.value());
    if (!GTK_IS_WINDOW(window)) {
      Py_RETURN_FALSE;
    }
    GtkWidget *menu_bar = nullptr;
    auto bar_found = handle->menu_containers.find("__menubar");
    if (bar_found != handle->menu_containers.end()) {
      menu_bar = static_cast<GtkWidget *>(bar_found->second);
    } else {
      menu_bar = gtk_menu_bar_new();
      auto browser = handle->window->browser_controller();
      browser.ensure_ok();
      auto *webview = static_cast<GtkWidget *>(browser.value());
      g_object_ref(webview);
      GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
      gtk_container_remove(GTK_CONTAINER(window), webview);
      gtk_container_add(GTK_CONTAINER(window), box);
      gtk_box_pack_start(GTK_BOX(box), menu_bar, FALSE, FALSE, 0);
      gtk_box_pack_start(GTK_BOX(box), webview, TRUE, TRUE, 0);
      gtk_widget_show_all(box);
      g_object_unref(webview);
      handle->menu_containers["__menubar"] = menu_bar;
    }

    auto ensure_menu = [&](const std::vector<std::string> &menu_path) -> GtkWidget * {
      GtkWidget *parent = menu_bar;
      std::vector<std::string> key_parts;
      for (const auto &part : menu_path) {
        key_parts.push_back(part);
        std::string key = menu_key(key_parts);
        auto found = handle->menu_containers.find(key);
        if (found != handle->menu_containers.end()) {
          parent = static_cast<GtkWidget *>(found->second);
          continue;
        }
        GtkWidget *item = gtk_menu_item_new_with_label(part.c_str());
        GtkWidget *submenu = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
        gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
        gtk_widget_show(item);
        handle->menu_containers[key] = submenu;
        parent = submenu;
      }
      return parent;
    };

    GtkWidget *parent = ensure_menu(path);
    if (separator) {
      GtkWidget *item = gtk_separator_menu_item_new();
      gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
      gtk_widget_show(item);
    } else if (callable == Py_None) {
      std::vector<std::string> submenu_path = path;
      submenu_path.push_back(label);
      ensure_menu(submenu_path);
    } else {
      GtkWidget *item = gtk_menu_item_new_with_label(label.c_str());
      gtk_widget_set_sensitive(item, enabled != 0);
      g_signal_connect(G_OBJECT(item), "activate",
                       G_CALLBACK(+[](GtkWidget *, gpointer data) {
                         invoke_menu_callback(GPOINTER_TO_INT(data));
                       }),
                       GINT_TO_POINTER(item_id));
      gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
      gtk_widget_show(item);
    }
    Py_RETURN_TRUE;
#elif defined(__linux__)
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
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

PyObject *native_set_backdrop_effect(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *effect_name = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &effect_name)) {
    return nullptr;
  }

  BackdropEffect effect = BackdropEffect::EffectNone;
  if (!parse_backdrop_effect(effect_name, &effect)) {
    PyErr_SetString(PyExc_ValueError,
                    "backdrop effect must be one of: none, acrylic, mica, glass");
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
    int backdrop = 1;  // DWMSBT_NONE
    if (effect == BackdropEffect::EffectMica) {
      backdrop = 2;  // DWMSBT_MAINWINDOW
    } else if (effect == BackdropEffect::EffectAcrylic) {
      backdrop = 3;  // DWMSBT_TRANSIENTWINDOW
    } else if (effect == BackdropEffect::EffectGlass) {
      backdrop = 4;  // DWMSBT_TABBEDWINDOW
    }

    HRESULT ok = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE,
                                       &backdrop, sizeof(backdrop));
    if (SUCCEEDED(ok)) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    using IdFn = id (*)(id, SEL);
    id content_view = reinterpret_cast<IdFn>(objc_msgSend)(
        window, sel_registerName("contentView"));
    if (!content_view) {
      Py_RETURN_FALSE;
    }

    using BoolFn = void (*)(id, SEL, bool);
    reinterpret_cast<BoolFn>(objc_msgSend)(window, sel_registerName("setOpaque:"), false);

    id color = cocoa_send_id(cocoa_class("NSColor"), "clearColor");
    using SetColorFn = void (*)(id, SEL, id);
    reinterpret_cast<SetColorFn>(objc_msgSend)(
        window, sel_registerName("setBackgroundColor:"), color);

    if (effect == BackdropEffect::EffectNone) {
      Py_RETURN_TRUE;
    }

    id effect_view_class = cocoa_class("NSVisualEffectView");
    id effect_view = cocoa_send_id(effect_view_class, "alloc");
    CocoaRect frame{{0.0, 0.0}, {10000.0, 10000.0}};
    using InitFrameFn = id (*)(id, SEL, CocoaRect);
    effect_view = reinterpret_cast<InitFrameFn>(objc_msgSend)(
        effect_view, sel_registerName("initWithFrame:"), frame);
    if (!effect_view) {
      Py_RETURN_FALSE;
    }

    long material = 60;  // NSVisualEffectMaterialPopover
    if (effect == BackdropEffect::EffectMica) {
      material = 12;  // NSVisualEffectMaterialWindowBackground
    } else if (effect == BackdropEffect::EffectGlass) {
      material = 6;  // NSVisualEffectMaterialUltraThin
    }

    using LongFn = void (*)(id, SEL, long);
    reinterpret_cast<LongFn>(objc_msgSend)(
        effect_view, sel_registerName("setMaterial:"), material);
    reinterpret_cast<LongFn>(objc_msgSend)(
        effect_view, sel_registerName("setBlendingMode:"), 0L);
    reinterpret_cast<LongFn>(objc_msgSend)(
        effect_view, sel_registerName("setState:"), 1L);
    reinterpret_cast<LongFn>(objc_msgSend)(
        effect_view, sel_registerName("setAutoresizingMask:"), 18L);

    using AddSubviewFn = void (*)(id, SEL, id, long, id);
    reinterpret_cast<AddSubviewFn>(objc_msgSend)(
        content_view, sel_registerName("addSubview:positioned:relativeTo:"),
        effect_view, -1L, nil);
    cocoa_release(effect_view);
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (!GTK_IS_WINDOW(window)) {
      Py_RETURN_FALSE;
    }
#if GTK_MAJOR_VERSION < 4
    gtk_widget_set_app_paintable(window, effect != BackdropEffect::EffectNone);
#endif
    gtk_widget_set_opacity(window, effect == BackdropEffect::EffectNone ? 1.0 : 0.94);
    Py_RETURN_TRUE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_set_background_color(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *color_text = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &color_text)) {
    return nullptr;
  }

  RgbaColor color{};
  try {
    if (!parse_background_color(color_text, &color)) {
      PyErr_SetString(PyExc_ValueError,
                      "background color must be #RRGGBB, #RRGGBBAA, "
                      "rgb(...), rgba(...), or transparent");
      return nullptr;
    }
  } catch (const std::exception &) {
    PyErr_SetString(PyExc_ValueError,
                    "background color must be #RRGGBB, #RRGGBBAA, "
                    "rgb(...), rgba(...), or transparent");
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
    HBRUSH brush = CreateSolidBrush(RGB(color.red, color.green, color.blue));
    if (!brush) {
      Py_RETURN_FALSE;
    }

    HBRUSH previous = handle->background_brush;
    handle->background_brush = brush;
    SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(brush));
    if (previous) {
      DeleteObject(previous);
    }

    MARGINS margins{};
    if (color.alpha < 255) {
      margins = {-1, -1, -1, -1};
    }
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    COLORREF caption_color = RGB(color.red, color.green, color.blue);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption_color,
                          sizeof(caption_color));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &caption_color,
                          sizeof(caption_color));

    int brightness = static_cast<int>(color.red) * 299 +
                     static_cast<int>(color.green) * 587 +
                     static_cast<int>(color.blue) * 114;
    COLORREF text_color = brightness < 128000 ? RGB(255, 255, 255) : RGB(0, 0, 0);
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text_color, sizeof(text_color));

    InvalidateRect(hwnd, nullptr, TRUE);
    Py_RETURN_TRUE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    using BoolFn = void (*)(id, SEL, bool);
    reinterpret_cast<BoolFn>(objc_msgSend)(
        window, sel_registerName("setOpaque:"), color.alpha == 255);

    id color_class = cocoa_class("NSColor");
    using ColorFn = id (*)(id, SEL, double, double, double, double);
    id ns_color = reinterpret_cast<ColorFn>(objc_msgSend)(
        color_class,
        sel_registerName("colorWithCalibratedRed:green:blue:alpha:"),
        static_cast<double>(color.red) / 255.0,
        static_cast<double>(color.green) / 255.0,
        static_cast<double>(color.blue) / 255.0,
        static_cast<double>(color.alpha) / 255.0);
    using SetColorFn = void (*)(id, SEL, id);
    reinterpret_cast<SetColorFn>(objc_msgSend)(
        window, sel_registerName("setBackgroundColor:"), ns_color);
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (!GTK_IS_WINDOW(window)) {
      Py_RETURN_FALSE;
    }
    char css[160];
    snprintf(css, sizeof(css),
             "window, .background { background-color: rgba(%u, %u, %u, %.4f); }",
             static_cast<unsigned int>(color.red),
             static_cast<unsigned int>(color.green),
             static_cast<unsigned int>(color.blue),
             static_cast<double>(color.alpha) / 255.0);
    GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_MAJOR_VERSION < 4
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(
        context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(window), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif
    g_object_unref(provider);
    Py_RETURN_TRUE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_set_border_color(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *color_text = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &color_text)) {
    return nullptr;
  }

  RgbaColor color{};
  try {
    if (!parse_background_color(color_text, &color)) {
      PyErr_SetString(PyExc_ValueError,
                      "border color must be #RRGGBB, #RRGGBBAA, "
                      "rgb(...), rgba(...), or transparent");
      return nullptr;
    }
  } catch (const std::exception &) {
    PyErr_SetString(PyExc_ValueError,
                    "border color must be #RRGGBB, #RRGGBBAA, "
                    "rgb(...), rgba(...), or transparent");
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
    COLORREF border_color = RGB(color.red, color.green, color.blue);
    HRESULT ok = DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border_color,
                                       sizeof(border_color));
    if (SUCCEEDED(ok)) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    Py_RETURN_FALSE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (!GTK_IS_WINDOW(window)) {
      Py_RETURN_FALSE;
    }
    char css[192];
    snprintf(css, sizeof(css),
             "window, .background { border: 1px solid rgba(%u, %u, %u, %.4f); }",
             static_cast<unsigned int>(color.red),
             static_cast<unsigned int>(color.green),
             static_cast<unsigned int>(color.blue),
             static_cast<double>(color.alpha) / 255.0);
    GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_MAJOR_VERSION < 4
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(
        context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(window), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif
    g_object_unref(provider);
    Py_RETURN_TRUE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_set_window_captions(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *background_text = nullptr;
  const char *symbol_text = nullptr;
  const char *border_text = nullptr;
  int height = 0;
  int button_size = 0;
  int visible = -1;
  if (!PyArg_ParseTuple(args, "Ozzz|iii", &capsule, &background_text, &symbol_text,
                        &border_text, &height, &button_size, &visible)) {
    return nullptr;
  }

  RgbaColor background{};
  RgbaColor symbol{};
  RgbaColor border{};
  bool has_background = false;
  bool has_symbol = false;
  bool has_border = false;
  try {
    if (!parse_optional_color(background_text, &background, &has_background) ||
        !parse_optional_color(symbol_text, &symbol, &has_symbol) ||
        !parse_optional_color(border_text, &border, &has_border)) {
      PyErr_SetString(PyExc_ValueError,
                      "caption colors must be #RRGGBB, #RRGGBBAA, "
                      "rgb(...), rgba(...), transparent, or None");
      return nullptr;
    }
  } catch (const std::exception &) {
    PyErr_SetString(PyExc_ValueError,
                    "caption colors must be #RRGGBB, #RRGGBBAA, "
                    "rgb(...), rgba(...), transparent, or None");
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
    bool applied = false;
    if (visible >= 0) {
      LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
      if (visible != 0) {
        style |= (WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
      } else {
        style &= ~(WS_CAPTION | WS_THICKFRAME);
        style |= (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
      }
      SetWindowLongPtrW(hwnd, GWL_STYLE, style);
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
      applied = true;
    }
    if (has_background) {
      COLORREF value = RGB(background.red, background.green, background.blue);
      applied = SUCCEEDED(DwmSetWindowAttribute(
                    hwnd, DWMWA_CAPTION_COLOR, &value, sizeof(value))) ||
                applied;
    }
    if (has_symbol) {
      COLORREF value = RGB(symbol.red, symbol.green, symbol.blue);
      applied = SUCCEEDED(DwmSetWindowAttribute(
                    hwnd, DWMWA_TEXT_COLOR, &value, sizeof(value))) ||
                applied;
    }
    if (has_border) {
      COLORREF value = RGB(border.red, border.green, border.blue);
      applied = SUCCEEDED(DwmSetWindowAttribute(
                    hwnd, DWMWA_BORDER_COLOR, &value, sizeof(value))) ||
                applied;
    }
    if (applied) {
      InvalidateRect(hwnd, nullptr, TRUE);
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    bool applied = false;
    if (visible >= 0) {
      using ButtonFn = id (*)(id, SEL, unsigned long long);
      using HiddenFn = void (*)(id, SEL, bool);
      unsigned long long button_types[] = {0ULL, 1ULL, 2ULL};
      for (unsigned long long button_type : button_types) {
        id button = reinterpret_cast<ButtonFn>(objc_msgSend)(
            window, sel_registerName("standardWindowButton:"), button_type);
        if (button) {
          reinterpret_cast<HiddenFn>(objc_msgSend)(
              button, sel_registerName("setHidden:"), visible == 0);
          applied = true;
        }
      }
    }
    if (has_background) {
      using BoolFn = void (*)(id, SEL, bool);
      reinterpret_cast<BoolFn>(objc_msgSend)(
          window, sel_registerName("setTitlebarAppearsTransparent:"), true);
      reinterpret_cast<BoolFn>(objc_msgSend)(
          window, sel_registerName("setOpaque:"), background.alpha == 255);

      id color_class = cocoa_class("NSColor");
      using ColorFn = id (*)(id, SEL, double, double, double, double);
      id ns_color = reinterpret_cast<ColorFn>(objc_msgSend)(
          color_class,
          sel_registerName("colorWithCalibratedRed:green:blue:alpha:"),
          static_cast<double>(background.red) / 255.0,
          static_cast<double>(background.green) / 255.0,
          static_cast<double>(background.blue) / 255.0,
          static_cast<double>(background.alpha) / 255.0);
      using SetColorFn = void (*)(id, SEL, id);
      reinterpret_cast<SetColorFn>(objc_msgSend)(
          window, sel_registerName("setBackgroundColor:"), ns_color);
      applied = true;
    }
    if (applied) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__linux__)
    Py_RETURN_FALSE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_set_shadow(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  PyObject *style_object = Py_True;
  if (!PyArg_ParseTuple(args, "O|O", &capsule, &style_object)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  bool enabled = true;
  std::string style = "system";
  if (PyBool_Check(style_object)) {
    enabled = style_object == Py_True;
    style = enabled ? "system" : "none";
  } else if (style_object == Py_None) {
    enabled = false;
    style = "none";
  } else if (PyUnicode_Check(style_object)) {
    const char *style_text = PyUnicode_AsUTF8(style_object);
    if (!style_text) {
      return nullptr;
    }
    style = normalized_text(style_text);
    enabled = !(style == "none" || style == "off" || style == "false" || style == "0");
  } else {
    PyErr_SetString(PyExc_TypeError, "shadow style must be bool, str, or None");
    return nullptr;
  }

  if (!(style == "none" || style == "off" || style == "false" || style == "0" ||
        style == "system" || style == "default" || style == "small" ||
        style == "medium" || style == "large")) {
    PyErr_SetString(PyExc_ValueError,
                    "shadow style must be one of: none, system, small, medium, large");
    return nullptr;
  }

  handle->shadow_enabled = enabled;
  handle->shadow_style = style;

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    int policy = enabled ? 2 : 1;  // DWMNCRP_ENABLED / DWMNCRP_DISABLED
    HRESULT ok = DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY,
                                       &policy, sizeof(policy));
    if (SUCCEEDED(ok)) {
      SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    using Fn = void (*)(id, SEL, bool);
    reinterpret_cast<Fn>(objc_msgSend)(window, sel_registerName("setHasShadow:"), enabled);
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (!GTK_IS_WINDOW(window)) {
      Py_RETURN_FALSE;
    }

    const char *shadow_css = "none";
    if (enabled) {
      if (style == "small") {
        shadow_css = "0 3px 12px rgba(0,0,0,0.24)";
      } else if (style == "large") {
        shadow_css = "0 18px 54px rgba(0,0,0,0.38)";
      } else {
        shadow_css = "0 10px 32px rgba(0,0,0,0.32)";
      }
    }

    char css[192];
    snprintf(css, sizeof(css), "window { box-shadow: %s; }", shadow_css);
    GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_MAJOR_VERSION < 4
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(
        context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(window), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif
    g_object_unref(provider);
    Py_RETURN_TRUE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_set_rounded_corners(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int radius = 8;
  if (!PyArg_ParseTuple(args, "Oi", &capsule, &radius)) {
    return nullptr;
  }
  if (radius < 0) {
    PyErr_SetString(PyExc_ValueError, "rounded corner radius must be >= 0");
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }
  handle->rounded_corner_radius = radius;

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(_WIN32)
    auto *hwnd = static_cast<HWND>(result.value());
    int preference = 1;  // DWMWCP_DONOTROUND
    if (radius > 0) {
      preference = radius <= 8 ? 3 : 2;  // DWMWCP_ROUNDSMALL / DWMWCP_ROUND
    }
    HRESULT ok = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE,
                                       &preference, sizeof(preference));
    if (SUCCEEDED(ok)) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    id content_view = cocoa_send_id(window, "contentView");
    if (!content_view) {
      Py_RETURN_FALSE;
    }

    using BoolFn = void (*)(id, SEL, bool);
    reinterpret_cast<BoolFn>(objc_msgSend)(
        content_view, sel_registerName("setWantsLayer:"), true);
    id layer = cocoa_send_id(content_view, "layer");
    if (!layer) {
      Py_RETURN_FALSE;
    }
    using DoubleFn = void (*)(id, SEL, double);
    reinterpret_cast<DoubleFn>(objc_msgSend)(
        layer, sel_registerName("setCornerRadius:"), static_cast<double>(radius));
    reinterpret_cast<BoolFn>(objc_msgSend)(
        layer, sel_registerName("setMasksToBounds:"), radius > 0);
    reinterpret_cast<BoolFn>(objc_msgSend)(
        window, sel_registerName("setOpaque:"), radius == 0);
    Py_RETURN_TRUE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (!GTK_IS_WINDOW(window)) {
      Py_RETURN_FALSE;
    }
    char css[192];
    snprintf(css, sizeof(css),
             "window, window.background, .background { border-radius: %dpx; }",
             radius);
    GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_MAJOR_VERSION < 4
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    GtkStyleContext *context = gtk_widget_get_style_context(window);
    gtk_style_context_add_provider(
        context, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#else
    gtk_css_provider_load_from_data(provider, css, -1);
    gtk_style_context_add_provider_for_display(
        gtk_widget_get_display(window), GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif
    g_object_unref(provider);
    Py_RETURN_TRUE;
#else
    Py_RETURN_FALSE;
#endif
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_get_rounded_corners(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  return PyLong_FromLong(handle->rounded_corner_radius);
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

PyObject *native_set_window_capability(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *name_text = nullptr;
  int enabled = 1;
  if (!PyArg_ParseTuple(args, "Osp", &capsule, &name_text, &enabled)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  try {
    if (apply_window_capability(handle, normalized_text(name_text), enabled != 0)) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_get_window_capability(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  const char *name_text = nullptr;
  if (!PyArg_ParseTuple(args, "Os", &capsule, &name_text)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }

  bool enabled = false;
  if (!get_window_capability_state(handle, normalized_text(name_text), &enabled)) {
    PyErr_SetString(PyExc_ValueError, "unknown window capability");
    return nullptr;
  }
  if (enabled) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
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
  if (!handle->movable) {
    Py_RETURN_FALSE;
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
  if (!handle->minimizable) {
    Py_RETURN_FALSE;
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
  if (!handle->maximizable) {
    Py_RETURN_FALSE;
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
  if (!handle->maximizable) {
    Py_RETURN_FALSE;
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
  if (fullscreen && !handle->fullscreenable) {
    Py_RETURN_FALSE;
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

PyObject *native_set_fullscreenable(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  int fullscreenable = 1;
  if (!PyArg_ParseTuple(args, "Op", &capsule, &fullscreenable)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }
  handle->fullscreenable = fullscreenable != 0;

  try {
    auto result = handle->window->window();
    result.ensure_ok();
#if defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    constexpr unsigned long long full_screen_primary = 1ULL << 7;
    constexpr unsigned long long full_screen_auxiliary = 1ULL << 8;
    auto behavior = cocoa_unsigned_long_long(window, "collectionBehavior");
    if (handle->fullscreenable) {
      behavior |= full_screen_primary;
    } else {
      behavior &= ~(full_screen_primary | full_screen_auxiliary);
    }
    using Fn = void (*)(id, SEL, unsigned long long);
    reinterpret_cast<Fn>(objc_msgSend)(
        window, sel_registerName("setCollectionBehavior:"), behavior);
#endif
    Py_RETURN_TRUE;
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_is_fullscreenable(PyObject *, PyObject *args) {
  PyObject *capsule = nullptr;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return nullptr;
  }

  auto *handle = get_handle(capsule);
  if (!handle) {
    return nullptr;
  }
  if (handle->fullscreenable) {
    Py_RETURN_TRUE;
  }
  Py_RETURN_FALSE;
}

PyObject *native_is_fullscreen(PyObject *, PyObject *args) {
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
    if (handle->fullscreen) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    auto mask = cocoa_unsigned_long_long(window, "styleMask");
    if ((mask & (1ULL << 14)) != 0) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__linux__) && GTK_MAJOR_VERSION < 4
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      auto *gdk_window = gtk_widget_get_window(window);
      if (gdk_window &&
          (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_FULLSCREEN) != 0) {
        Py_RETURN_TRUE;
      }
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

PyObject *native_is_maximized(PyObject *, PyObject *args) {
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
    if (IsZoomed(static_cast<HWND>(result.value())) != FALSE) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    if (cocoa_bool(window, "isZoomed")) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__linux__) && GTK_MAJOR_VERSION < 4
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      auto *gdk_window = gtk_widget_get_window(window);
      if (gdk_window &&
          (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_MAXIMIZED) != 0) {
        Py_RETURN_TRUE;
      }
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

PyObject *native_is_minimized(PyObject *, PyObject *args) {
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
    if (IsIconic(static_cast<HWND>(result.value())) != FALSE) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    if (cocoa_bool(window, "isMiniaturized")) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__linux__) && GTK_MAJOR_VERSION < 4
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window)) {
      auto *gdk_window = gtk_widget_get_window(window);
      if (gdk_window &&
          (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_ICONIFIED) != 0) {
        Py_RETURN_TRUE;
      }
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

PyObject *native_is_visible(PyObject *, PyObject *args) {
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
    if (IsWindowVisible(static_cast<HWND>(result.value())) != FALSE) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    if (cocoa_bool(window, "isVisible")) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (gtk_widget_get_visible(window)) {
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

PyObject *native_get_window_handle(PyObject *, PyObject *args) {
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
    return PyLong_FromVoidPtr(result.value());
  } catch (const std::exception &e) {
    raise_runtime_error(e);
    return nullptr;
  }
}

PyObject *native_is_frameless(PyObject *, PyObject *args) {
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
    auto style = GetWindowLongPtrW(static_cast<HWND>(result.value()), GWL_STYLE);
    if ((style & WS_CAPTION) == 0) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    auto style = cocoa_unsigned_long_long(window, "styleMask");
    if ((style & 1ULL) == 0) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#elif defined(__linux__)
    auto *window = static_cast<GtkWidget *>(result.value());
    if (GTK_IS_WINDOW(window) && !gtk_window_get_decorated(GTK_WINDOW(window))) {
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

PyObject *native_has_shadow(PyObject *, PyObject *args) {
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
#if defined(__APPLE__)
    auto window = static_cast<id>(result.value());
    if (cocoa_bool(window, "hasShadow")) {
      Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
#else
    if (handle->shadow_enabled) {
      Py_RETURN_TRUE;
    }
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
  if (!handle->movable) {
    Py_RETURN_FALSE;
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
    {"set_devtools_enabled", reinterpret_cast<PyCFunction>(native_set_devtools_enabled),
     METH_VARARGS, "Toggle native webview developer tools and debug accelerators."},
    {"is_devtools_enabled", reinterpret_cast<PyCFunction>(native_is_devtools_enabled),
     METH_VARARGS, "Return whether native webview developer tools are enabled."},
    {"set_hardware_acceleration_enabled",
     reinterpret_cast<PyCFunction>(native_set_hardware_acceleration_enabled),
     METH_VARARGS, "Toggle native webview hardware acceleration settings."},
    {"is_hardware_acceleration_enabled",
     reinterpret_cast<PyCFunction>(native_is_hardware_acceleration_enabled),
     METH_VARARGS, "Return whether hardware acceleration is enabled."},
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
    {"add_menu_item", reinterpret_cast<PyCFunction>(native_add_menu_item), METH_VARARGS,
     "Add a native menubar item, submenu, or separator."},
    {"set_icon", reinterpret_cast<PyCFunction>(native_set_icon), METH_VARARGS,
     "Set the native window icon."},
    {"set_frameless", reinterpret_cast<PyCFunction>(native_set_frameless), METH_VARARGS,
     "Toggle native window frame decorations."},
    {"set_backdrop_effect", reinterpret_cast<PyCFunction>(native_set_backdrop_effect),
     METH_VARARGS, "Set native backdrop material/effect."},
    {"set_background_color", reinterpret_cast<PyCFunction>(native_set_background_color),
     METH_VARARGS, "Set native window background color."},
    {"set_border_color", reinterpret_cast<PyCFunction>(native_set_border_color),
     METH_VARARGS, "Set native window border color."},
    {"set_window_captions", reinterpret_cast<PyCFunction>(native_set_window_captions),
     METH_VARARGS, "Set native window caption/control appearance."},
    {"set_shadow", reinterpret_cast<PyCFunction>(native_set_shadow), METH_VARARGS,
     "Set native window shadow state/style."},
    {"set_rounded_corners", reinterpret_cast<PyCFunction>(native_set_rounded_corners),
     METH_VARARGS, "Set native window rounded corner radius."},
    {"get_rounded_corners", reinterpret_cast<PyCFunction>(native_get_rounded_corners),
     METH_VARARGS, "Return native window rounded corner radius."},
    {"set_visible", reinterpret_cast<PyCFunction>(native_set_visible), METH_VARARGS,
     "Toggle native window visibility."},
    {"set_window_capability", reinterpret_cast<PyCFunction>(native_set_window_capability),
     METH_VARARGS, "Toggle a native window capability flag."},
    {"get_window_capability", reinterpret_cast<PyCFunction>(native_get_window_capability),
     METH_VARARGS, "Return a native window capability flag."},
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
    {"set_fullscreenable", reinterpret_cast<PyCFunction>(native_set_fullscreenable),
     METH_VARARGS, "Toggle whether native fullscreen is allowed."},
    {"is_fullscreenable", reinterpret_cast<PyCFunction>(native_is_fullscreenable),
     METH_VARARGS, "Return whether native fullscreen is allowed."},
    {"is_fullscreen", reinterpret_cast<PyCFunction>(native_is_fullscreen), METH_VARARGS,
     "Return whether the native window is fullscreen."},
    {"is_maximized", reinterpret_cast<PyCFunction>(native_is_maximized), METH_VARARGS,
     "Return whether the native window is maximized."},
    {"is_minimized", reinterpret_cast<PyCFunction>(native_is_minimized), METH_VARARGS,
     "Return whether the native window is minimized."},
    {"is_visible", reinterpret_cast<PyCFunction>(native_is_visible), METH_VARARGS,
     "Return whether the native window is visible."},
    {"get_window_handle", reinterpret_cast<PyCFunction>(native_get_window_handle),
     METH_VARARGS, "Return the native platform window handle pointer."},
    {"is_frameless", reinterpret_cast<PyCFunction>(native_is_frameless), METH_VARARGS,
     "Return whether the native window is frameless."},
    {"has_shadow", reinterpret_cast<PyCFunction>(native_has_shadow), METH_VARARGS,
     "Return whether the native window shadow is enabled."},
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
