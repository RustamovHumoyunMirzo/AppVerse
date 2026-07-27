from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
import json
import os
import re
from typing import Any, Callable, DefaultDict, Final


if os.name == "nt" and hasattr(os, "add_dll_directory"):
    os.add_dll_directory(str(Path(__file__).resolve().parent))

from . import _native


HINT_NONE: Final[int] = _native.HINT_NONE
HINT_MIN: Final[int] = _native.HINT_MIN
HINT_MAX: Final[int] = _native.HINT_MAX
HINT_FIXED: Final[int] = _native.HINT_FIXED

START: Final[str] = "start"
READY: Final[str] = "ready"
CLOSE: Final[str] = "close"
SHOW: Final[str] = "show"
HIDE: Final[str] = "hide"
DESTROY: Final[str] = "destroy"
MESSAGE: Final[str] = "message"
ERROR: Final[str] = "error"
MENU: Final[str] = "menu"

BACKDROP_NONE: Final[str] = "none"
BACKDROP_ACRYLIC: Final[str] = "acrylic"
BACKDROP_MICA: Final[str] = "mica"
BACKDROP_GLASS: Final[str] = "glass"
READY_TO_SHOW: Final[str] = "ready_to_show"


class Color:
    TRANSPARENT: Final[str] = "#00000000"
    WHITE: Final[str] = "#ffffff"
    BLACK: Final[str] = "#000000"


APPVERSE_BRIDGE_JS = r"""
(() => {
  if (window.appverse) return;

  const listeners = new Map();
  const emit = (name, detail) => {
    const event = { name, detail };
    (listeners.get(name) || []).forEach((fn) => fn(event));
    (listeners.get("*") || []).forEach((fn) => fn(event));
  };

  window.appverse = {
    call(name, ...args) {
      if (typeof window[name] !== "function") {
        return Promise.reject(new Error(`AppVerse binding '${name}' is not registered`));
      }
      return window[name](...args);
    },
    send(name, detail) {
      return window.appverse.call("__appverse_message", name, detail ?? null);
    },
    on(name, fn) {
      const current = listeners.get(name) || [];
      current.push(fn);
      listeners.set(name, current);
      return () => window.appverse.off(name, fn);
    },
    off(name, fn) {
      listeners.set(name, (listeners.get(name) || []).filter((item) => item !== fn));
    },
    emit,
  };

  const getRegion = (element) => {
    for (let node = element; node && node !== document; node = node.parentElement) {
      const style = window.getComputedStyle(node);
      const region = style.getPropertyValue("-webkit-app-region") || style.getPropertyValue("app-region");
      if (region === "no-drag") return "no-drag";
      if (region === "drag") return "drag";
    }
    return "none";
  };

  window.addEventListener("pointerdown", (event) => {
    if (event.button !== 0 && event.button !== 2) return;
    const target = event.target;
    if (!(target instanceof Element)) return;
    const tag = target.tagName.toLowerCase();
    if (["button", "input", "select", "textarea", "a"].includes(tag)) return;
    if (getRegion(target) !== "drag") return;
    event.preventDefault();
    if (event.button === 2) {
      window.appverse.call("__appverse_show_window_menu", event.screenX, event.screenY).catch(() => {});
      return;
    }
    if (event.detail > 1) return;
    window.appverse.call("__appverse_start_drag").catch(() => {});
  }, true);

  window.addEventListener("dblclick", (event) => {
    if (event.button !== 0) return;
    const target = event.target;
    if (!(target instanceof Element)) return;
    const tag = target.tagName.toLowerCase();
    if (["button", "input", "select", "textarea", "a"].includes(tag)) return;
    if (getRegion(target) !== "drag") return;
    event.preventDefault();
    window.appverse.call("__appverse_toggle_maximize").catch(() => {});
  }, true);

  window.addEventListener("contextmenu", (event) => {
    const target = event.target;
    if (!(target instanceof Element)) return;
    if (getRegion(target) !== "drag") return;
    event.preventDefault();
    window.appverse.call("__appverse_show_window_menu", event.screenX, event.screenY).catch(() => {});
  }, true);
})();
"""


READY_REVEAL_JS = r"""
(() => {
  const reveal = () => {
    document.documentElement.style.opacity = "1";
    document.documentElement.style.visibility = "visible";
    document.documentElement.style.background = "";
    if (window.appverse) {
      window.appverse.send("__appverse_ready_to_show", null).catch(() => {});
    }
  };

  document.documentElement.style.background = "#101418";
  document.documentElement.style.opacity = "0";
  document.documentElement.style.visibility = "hidden";
  document.documentElement.style.transition = "opacity 120ms ease";

  if (document.readyState === "complete" || document.readyState === "interactive") {
    queueMicrotask(reveal);
  } else {
    window.addEventListener("DOMContentLoaded", reveal, { once: true });
  }
})();
"""

BLOCK_FULLSCREEN_KEYS_JS = r"""
(() => {
  window.__appverse_fullscreenable = false;
  if (window.__appverse_fullscreen_guard_installed) return;
  window.__appverse_fullscreen_guard_installed = true;
  window.addEventListener("keydown", (event) => {
    if (window.__appverse_fullscreenable === false && event.key === "F11") {
      event.preventDefault();
      event.stopPropagation();
    }
  }, true);
})();
"""

@dataclass
class WindowOptions:
    title: str | None = None
    width: int = 960
    height: int = 640
    x: int | None = None
    y: int | None = None
    min_width: int | None = None
    min_height: int | None = None
    max_width: int | None = None
    max_height: int | None = None
    size_hint: int = HINT_NONE
    debug: bool = False
    devtools: bool = False
    hardware_acceleration: bool = True
    html: str | None = None
    url: str | None = None
    frameless: bool = False
    fullscreen: bool = False
    fullscreenable: bool = True
    resizable: bool = True
    movable: bool = True
    always_on_top: bool = False
    skip_taskbar: bool = False
    closable: bool = True
    minimizable: bool = True
    maximizable: bool = True
    rounded_corners: bool | int = True
    icon: str | os.PathLike[str] | None = None
    visible: bool = True
    show_when_ready: bool = False
    init_scripts: tuple[str, ...] = ()


EventHandler = Callable[..., Any]
BindingHandler = Callable[..., Any]


class Window:
    """Native webview-backed application window."""

    def __init__(
        self,
        *,
        title: str | None = None,
        width: int = 960,
        height: int = 640,
        x: int | None = None,
        y: int | None = None,
        min_width: int | None = None,
        min_height: int | None = None,
        max_width: int | None = None,
        max_height: int | None = None,
        size_hint: int = HINT_NONE,
        debug: bool = False,
        devtools: bool = False,
        hardware_acceleration: bool = True,
        html: str | None = None,
        url: str | None = None,
        frameless: bool = False,
        fullscreen: bool = False,
        fullscreenable: bool = True,
        resizable: bool = True,
        movable: bool = True,
        always_on_top: bool = False,
        skip_taskbar: bool = False,
        closable: bool = True,
        minimizable: bool = True,
        maximizable: bool = True,
        rounded_corners: bool | int = True,
        icon: str | os.PathLike[str] | None = None,
        visible: bool = True,
        show_when_ready: bool = False,
        init_scripts: tuple[str, ...] = (),
        options: WindowOptions | None = None,
    ) -> None:
        if options is None:
            options = WindowOptions(
                title=title,
                width=width,
                height=height,
                x=x,
                y=y,
                min_width=min_width,
                min_height=min_height,
                max_width=max_width,
                max_height=max_height,
                size_hint=size_hint,
                debug=debug,
                devtools=devtools,
                hardware_acceleration=hardware_acceleration,
                html=html,
                url=url,
                frameless=frameless,
                fullscreen=fullscreen,
                fullscreenable=fullscreenable,
                resizable=resizable,
                movable=movable,
                always_on_top=always_on_top,
                skip_taskbar=skip_taskbar,
                closable=closable,
                minimizable=minimizable,
                maximizable=maximizable,
                rounded_corners=rounded_corners,
                icon=icon,
                visible=visible,
                show_when_ready=show_when_ready,
                init_scripts=init_scripts,
            )

        self.options = options
        self._title_explicit = options.title is not None
        native_visible = options.visible and not options.show_when_ready
        self._handle = _native.create_window(
            debug=options.debug or options.devtools,
            visible=native_visible,
            hardware_acceleration=options.hardware_acceleration,
        )
        self._closed = False
        self._started = False
        self._visible = native_visible
        self._events: DefaultDict[str, list[EventHandler]] = defaultdict(list)
        self._bindings: dict[str, BindingHandler] = {}
        self._menu_next_id = 1
        self._menu_items: dict[int, dict[str, Any]] = {}

        self.init(APPVERSE_BRIDGE_JS)
        if options.show_when_ready:
            self.init(READY_REVEAL_JS)
        if not options.fullscreenable:
            self.init(BLOCK_FULLSCREEN_KEYS_JS)
        self.bind("__appverse_message", self._receive_message)
        self.bind("__appverse_start_drag", self._start_drag)
        self.bind("__appverse_show_window_menu", self._show_window_menu)
        self.bind("__appverse_toggle_maximize", self._toggle_maximize)
        if options.title is not None:
            self.set_title(options.title)
        self.set_size(options.width, options.height, options.size_hint)
        if options.x is not None and options.y is not None:
            self.set_position(options.x, options.y)
        if options.min_width is not None and options.min_height is not None:
            self.set_min_size(options.min_width, options.min_height)
        if options.max_width is not None and options.max_height is not None:
            self.set_max_size(options.max_width, options.max_height)

        for script in options.init_scripts:
            self.init(script)

        if options.frameless:
            self.set_frameless(True)
        self.set_fullscreenable(options.fullscreenable)
        self.set_resizable(options.resizable)
        self.set_movable(options.movable)
        self.set_always_on_top(options.always_on_top)
        self.set_skip_taskbar(options.skip_taskbar)
        self.set_closable(options.closable)
        self.set_minimizable(options.minimizable)
        self.set_maximizable(options.maximizable)
        self.set_rounded_corners(options.rounded_corners)
        if options.fullscreen:
            self.set_fullscreen(True)
        if options.icon is not None:
            self.set_icon(options.icon)
        if options.html is not None:
            self.set_html(options.html)
        if options.url is not None:
            self.navigate(options.url)

    def on(self, event: str, handler: EventHandler | None = None):
        def decorator(fn: EventHandler) -> EventHandler:
            self._events[event].append(fn)
            return fn

        if handler is None:
            return decorator
        return decorator(handler)

    def off(self, event: str, handler: EventHandler | None = None) -> None:
        if handler is None:
            self._events.pop(event, None)
            return
        self._events[event] = [item for item in self._events[event] if item != handler]

    def once(self, event: str, handler: EventHandler | None = None):
        def decorator(fn: EventHandler) -> EventHandler:
            def wrapper(*args: Any, **kwargs: Any) -> Any:
                self.off(event, wrapper)
                return fn(*args, **kwargs)

            self.on(event, wrapper)
            return fn

        if handler is None:
            return decorator
        return decorator(handler)

    def emit(self, event: str, *args: Any, **kwargs: Any) -> None:
        for handler in tuple(self._events.get(event, ())):
            handler(*args, **kwargs)
        for handler in tuple(self._events.get("*", ())):
            handler(event, *args, **kwargs)

    def set_title(self, title: str) -> None:
        _native.set_title(self._handle, title)

    def set_size(self, width: int, height: int, hint: int = HINT_NONE) -> None:
        _native.set_size(self._handle, width, height, hint)

    def set_min_size(self, width: int, height: int) -> None:
        self.set_size(width, height, HINT_MIN)

    def set_max_size(self, width: int, height: int) -> None:
        self.set_size(width, height, HINT_MAX)

    def set_fixed_size(self, width: int, height: int) -> None:
        self.set_size(width, height, HINT_FIXED)

    def set_position(self, x: int, y: int) -> bool:
        return bool(_native.set_position(self._handle, x, y))

    def set_icon(self, icon: str | os.PathLike[str]) -> bool:
        icon_path = self._prepare_icon(icon)
        return bool(_native.set_icon(self._handle, str(icon_path)))

    def set_frameless(self, frameless: bool = True) -> bool:
        return bool(_native.set_frameless(self._handle, frameless))

    def set_backdrop_effect(self, effect: str) -> bool:
        return bool(_native.set_backdrop_effect(self._handle, effect))

    def set_background_color(self, color: str) -> bool:
        return bool(_native.set_background_color(self._handle, color))

    def set_border_color(self, color: str) -> bool:
        return bool(_native.set_border_color(self._handle, color))

    def set_devtools_enabled(self, enabled: bool = True) -> bool:
        set_devtools_enabled = getattr(_native, "set_devtools_enabled", None)
        self.options.devtools = enabled
        self.options.debug = enabled
        if set_devtools_enabled is None:
            return False
        return bool(set_devtools_enabled(self._handle, enabled))

    def is_devtools_enabled(self) -> bool:
        is_devtools_enabled = getattr(_native, "is_devtools_enabled", None)
        if is_devtools_enabled is None:
            return self.options.debug or self.options.devtools
        return bool(is_devtools_enabled(self._handle))

    def set_hardware_acceleration_enabled(self, enabled: bool = True) -> bool:
        set_hardware_acceleration_enabled = getattr(
            _native, "set_hardware_acceleration_enabled", None
        )
        self.options.hardware_acceleration = enabled
        if set_hardware_acceleration_enabled is None:
            return False
        return bool(set_hardware_acceleration_enabled(self._handle, enabled))

    def is_hardware_acceleration_enabled(self) -> bool:
        is_hardware_acceleration_enabled = getattr(
            _native, "is_hardware_acceleration_enabled", None
        )
        if is_hardware_acceleration_enabled is None:
            return self.options.hardware_acceleration
        return bool(is_hardware_acceleration_enabled(self._handle))

    def set_shadow(self, style: str | bool = True) -> bool:
        set_shadow = getattr(_native, "set_shadow", None)
        if set_shadow is None:
            return False
        return bool(set_shadow(self._handle, style))

    def set_rounded_corners(self, radius: bool | int = True) -> bool:
        set_rounded_corners = getattr(_native, "set_rounded_corners", None)
        if isinstance(radius, bool):
            value = 8 if radius else 0
        else:
            value = int(radius)
        if value < 0:
            raise ValueError("rounded corner radius must be >= 0")
        self.options.rounded_corners = radius
        if set_rounded_corners is None:
            return False
        return bool(set_rounded_corners(self._handle, value))

    def get_rounded_corners(self) -> int:
        get_rounded_corners = getattr(_native, "get_rounded_corners", None)
        if get_rounded_corners is None:
            return 8 if self.options.rounded_corners is True else int(self.options.rounded_corners)
        return int(get_rounded_corners(self._handle))

    def has_rounded_corners(self) -> bool:
        return self.get_rounded_corners() > 0

    def set_window_captions(
        self,
        *,
        background: str | None = None,
        symbols: str | None = None,
        border: str | None = None,
        height: int | None = None,
        control_size: int | None = None,
        visible: bool | None = None,
    ) -> bool:
        set_window_captions = getattr(_native, "set_window_captions", None)
        if set_window_captions is None:
            return False
        return bool(
            set_window_captions(
                self._handle,
                background,
                symbols,
                border,
                height or 0,
                control_size or 0,
                -1 if visible is None else int(visible),
            )
        )

    def _set_capability(self, name: str, enabled: bool) -> bool:
        set_window_capability = getattr(_native, "set_window_capability", None)
        setattr(self.options, name, enabled)
        if set_window_capability is None:
            return False
        return bool(set_window_capability(self._handle, name, enabled))

    def _get_capability(self, name: str, fallback: bool) -> bool:
        get_window_capability = getattr(_native, "get_window_capability", None)
        if get_window_capability is None:
            return fallback
        return bool(get_window_capability(self._handle, name))

    def set_resizable(self, resizable: bool = True) -> bool:
        return self._set_capability("resizable", resizable)

    def is_resizable(self) -> bool:
        return self._get_capability("resizable", self.options.resizable)

    def set_movable(self, movable: bool = True) -> bool:
        return self._set_capability("movable", movable)

    def is_movable(self) -> bool:
        return self._get_capability("movable", self.options.movable)

    def set_always_on_top(self, always_on_top: bool = True) -> bool:
        return self._set_capability("always_on_top", always_on_top)

    def is_always_on_top(self) -> bool:
        return self._get_capability("always_on_top", self.options.always_on_top)

    def set_skip_taskbar(self, skip_taskbar: bool = True) -> bool:
        return self._set_capability("skip_taskbar", skip_taskbar)

    def is_skip_taskbar(self) -> bool:
        return self._get_capability("skip_taskbar", self.options.skip_taskbar)

    def set_hidden_taskbar(self, hidden: bool = True) -> bool:
        return self.set_skip_taskbar(hidden)

    def is_hidden_taskbar(self) -> bool:
        return self.is_skip_taskbar()

    def set_closable(self, closable: bool = True) -> bool:
        return self._set_capability("closable", closable)

    def is_closable(self) -> bool:
        return self._get_capability("closable", self.options.closable)

    def set_minimizable(self, minimizable: bool = True) -> bool:
        return self._set_capability("minimizable", minimizable)

    def is_minimizable(self) -> bool:
        return self._get_capability("minimizable", self.options.minimizable)

    def set_maximizable(self, maximizable: bool = True) -> bool:
        return self._set_capability("maximizable", maximizable)

    def is_maximizable(self) -> bool:
        return self._get_capability("maximizable", self.options.maximizable)

    def is_visible(self) -> bool:
        is_visible = getattr(_native, "is_visible", None)
        if is_visible is None:
            return self._visible
        return bool(is_visible(self._handle))

    def get_handle(self) -> int:
        get_window_handle = getattr(_native, "get_window_handle", None)
        if get_window_handle is None:
            return 0
        return int(get_window_handle(self._handle))

    def is_frameless(self) -> bool:
        is_frameless = getattr(_native, "is_frameless", None)
        if is_frameless is None:
            return self.options.frameless
        return bool(is_frameless(self._handle))

    def has_shadow(self) -> bool:
        has_shadow = getattr(_native, "has_shadow", None)
        if has_shadow is None:
            return True
        return bool(has_shadow(self._handle))

    @property
    def hasShadow(self) -> bool:
        return self.has_shadow()

    def is_minimized(self) -> bool:
        is_minimized = getattr(_native, "is_minimized", None)
        if is_minimized is None:
            return False
        return bool(is_minimized(self._handle))

    def is_maximized(self) -> bool:
        is_maximized = getattr(_native, "is_maximized", None)
        if is_maximized is None:
            return False
        return bool(is_maximized(self._handle))

    def is_fullscreen(self) -> bool:
        is_fullscreen = getattr(_native, "is_fullscreen", None)
        if is_fullscreen is None:
            return False
        return bool(is_fullscreen(self._handle))

    def is_fullscreenable(self) -> bool:
        is_fullscreenable = getattr(_native, "is_fullscreenable", None)
        if is_fullscreenable is None:
            return self.options.fullscreenable
        return bool(is_fullscreenable(self._handle))

    def show(self) -> bool:
        applied = bool(_native.set_visible(self._handle, True))
        self._visible = True
        self.emit(SHOW, self, applied)
        return applied

    def hide(self) -> bool:
        applied = bool(_native.set_visible(self._handle, False))
        self._visible = False
        self.emit(HIDE, self, applied)
        return applied

    def minimize(self) -> bool:
        return bool(_native.minimize(self._handle))

    def maximize(self) -> bool:
        return bool(_native.maximize(self._handle))

    def restore(self) -> bool:
        return bool(_native.restore(self._handle))

    def toggle_maximize(self) -> bool:
        toggle_maximize = getattr(_native, "toggle_maximize", None)
        if toggle_maximize is None:
            return False
        return bool(toggle_maximize(self._handle))

    def set_fullscreen(self, fullscreen: bool = True) -> bool:
        applied = bool(_native.set_fullscreen(self._handle, fullscreen))
        if applied:
            self.options.fullscreen = fullscreen
        return applied

    def set_fullscreenable(self, fullscreenable: bool = True) -> bool:
        set_fullscreenable = getattr(_native, "set_fullscreenable", None)
        self.options.fullscreenable = fullscreenable
        if not fullscreenable:
            self.set_fullscreen(False)
            self.dispatch_eval(BLOCK_FULLSCREEN_KEYS_JS)
        else:
            self.dispatch_eval("window.__appverse_fullscreenable = true;")
        if set_fullscreenable is None:
            return fullscreenable
        return bool(set_fullscreenable(self._handle, fullscreenable))

    def start_drag(self) -> bool:
        start_drag = getattr(_native, "start_drag", None)
        if start_drag is None:
            return False
        return bool(start_drag(self._handle))

    def show_window_menu(self, x: int, y: int) -> bool:
        show_window_menu = getattr(_native, "show_window_menu", None)
        if show_window_menu is None:
            return False
        return bool(show_window_menu(self._handle, x, y))

    def open_devtools(self) -> bool:
        return self.is_devtools_enabled()

    def set_html(self, html: str) -> None:
        if not self._title_explicit:
            title = self._extract_title(html)
            if title:
                self.set_title(title)
        _native.set_html(self._handle, html)

    def load_html(self, path: str | os.PathLike[str], *, encoding: str = "utf-8") -> None:
        html_path = Path(path).expanduser().resolve()
        html = html_path.read_text(encoding=encoding)
        if not self._title_explicit:
            title = self._extract_title(html)
            if title:
                self.set_title(title)
        self.navigate(html_path.as_uri())

    def navigate(self, url: str) -> None:
        _native.navigate(self._handle, url)

    def init(self, javascript: str) -> None:
        _native.init(self._handle, javascript)

    def eval(self, javascript: str) -> None:
        _native.eval(self._handle, javascript)

    def dispatch_eval(self, javascript: str) -> None:
        _native.dispatch_eval(self._handle, javascript)

    def send(self, event: str, detail: Any = None) -> None:
        payload = json.dumps({"event": event, "detail": detail})
        self.dispatch_eval(f"window.appverse && window.appverse.emit({payload}.event, {payload}.detail)")

    def bind(self, name: str, handler: BindingHandler | None = None):
        def decorator(fn: BindingHandler) -> BindingHandler:
            self._bindings[name] = fn

            def adapter(_id: str, request: str) -> str:
                args = json.loads(request)
                if not isinstance(args, list):
                    args = [args]
                result = fn(*args)
                return json.dumps(result)

            _native.bind(self._handle, name, adapter)
            return fn

        if handler is None:
            return decorator
        return decorator(handler)

    def add_menu(self, label: str) -> bool:
        return bool(_native.add_menu_item(self._handle, (), label, 0, True, False, None))

    def add_submenu(self, path: tuple[str, ...] | list[str], label: str) -> bool:
        return bool(_native.add_menu_item(self._handle, tuple(path), label, 0, True, False, None))

    def add_menu_separator(self, path: tuple[str, ...] | list[str]) -> bool:
        return bool(_native.add_menu_item(self._handle, tuple(path), "", 0, True, True, None))

    def add_menu_item(
        self,
        path: tuple[str, ...] | list[str],
        label: str,
        handler: EventHandler | None = None,
        *,
        enabled: bool = True,
        item_id: int | None = None,
    ) -> int:
        if item_id is None:
            item_id = self._menu_next_id
            self._menu_next_id += 1
        if item_id <= 0:
            raise ValueError("menu item_id must be positive")

        info = {
            "id": item_id,
            "path": tuple(path),
            "label": label,
            "enabled": enabled,
        }
        self._menu_items[item_id] = info

        def adapter(native_item_id: int) -> None:
            item = self._menu_items.get(native_item_id, info)
            self.emit(MENU, self, item)
            self.emit(f"menu:{native_item_id}", self, item)
            if handler is not None:
                try:
                    handler(self, item)
                except TypeError:
                    handler()

        applied = bool(
            _native.add_menu_item(
                self._handle,
                tuple(path),
                label,
                item_id,
                enabled,
                False,
                adapter,
            )
        )
        if not applied:
            self._menu_items.pop(item_id, None)
            raise RuntimeError("native menu item could not be added")
        return item_id

    def unbind(self, name: str) -> None:
        self._bindings.pop(name, None)
        _native.unbind(self._handle, name)

    def terminate(self) -> None:
        self.emit(CLOSE, self)
        _native.terminate(self._handle)

    def run(self) -> None:
        self._started = True
        self.emit(START, self)
        self.emit(READY, self)
        if self.options.show_when_ready and self.options.visible and not self._visible:
            self.show()
        try:
            _native.run(self._handle)
        finally:
            self.emit(DESTROY, self)
            self._closed = True

    def destroy(self) -> None:
        if not self._closed:
            self.emit(DESTROY, self)
            _native.destroy(self._handle)
            self._closed = True

    def _receive_message(self, event: str, detail: Any = None) -> None:
        if event == "__appverse_ready_to_show":
            if self.options.visible and not self._visible:
                self.show()
            self.emit(READY_TO_SHOW, self)
            return
        self.emit(MESSAGE, event, detail)

    def _start_drag(self) -> bool:
        return self.start_drag()

    def _show_window_menu(self, x: int, y: int) -> bool:
        return self.show_window_menu(x, y)

    def _toggle_maximize(self) -> bool:
        return self.toggle_maximize()

    def _prepare_icon(self, icon: str | os.PathLike[str]) -> Path:
        icon_path = Path(icon).expanduser()
        if not icon_path.is_absolute():
            icon_path = (Path.cwd() / icon_path).resolve()
        else:
            icon_path = icon_path.resolve()

        if not icon_path.exists():
            raise FileNotFoundError(f"Icon file does not exist: {icon_path}")

        if os.name != "nt" or icon_path.suffix.lower() == ".ico":
            return icon_path

        try:
            from PIL import Image
        except ImportError:
            return icon_path

        cache_dir = Path(os.environ.get("APPVERSE_CACHE_DIR", Path.home() / ".appverse"))
        cache_dir.mkdir(parents=True, exist_ok=True)
        ico_path = cache_dir / f"{icon_path.stem}.ico"
        with Image.open(icon_path) as image:
            image.save(ico_path, format="ICO", sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
        return ico_path

    def _extract_title(self, html: str) -> str | None:
        match = re.search(r"<title[^>]*>(.*?)</title>", html, flags=re.IGNORECASE | re.DOTALL)
        if not match:
            return None
        title = re.sub(r"\s+", " ", match.group(1)).strip()
        return title or None

    def __enter__(self) -> "Window":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.destroy()


def create_window(**kwargs: Any) -> Window:
    return Window(**kwargs)


__all__ = [
    "APPVERSE_BRIDGE_JS",
    "CLOSE",
    "Color",
    "DESTROY",
    "ERROR",
    "HINT_FIXED",
    "HINT_MAX",
    "HINT_MIN",
    "HINT_NONE",
    "BACKDROP_ACRYLIC",
    "BACKDROP_GLASS",
    "BACKDROP_MICA",
    "BACKDROP_NONE",
    "MESSAGE",
    "READY",
    "READY_REVEAL_JS",
    "READY_TO_SHOW",
    "SHOW",
    "HIDE",
    "MENU",
    "START",
    "Window",
    "WindowOptions",
    "create_window",
]
