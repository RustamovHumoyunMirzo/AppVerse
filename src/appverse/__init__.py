from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
import json
import os
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
READY_TO_SHOW: Final[str] = "ready_to_show"


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


@dataclass
class WindowOptions:
    title: str = "AppVerse"
    width: int = 960
    height: int = 640
    size_hint: int = HINT_NONE
    debug: bool = False
    html: str | None = None
    url: str | None = None
    frameless: bool = False
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
        title: str = "AppVerse",
        width: int = 960,
        height: int = 640,
        size_hint: int = HINT_NONE,
        debug: bool = False,
        html: str | None = None,
        url: str | None = None,
        frameless: bool = False,
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
                size_hint=size_hint,
                debug=debug,
                html=html,
                url=url,
                frameless=frameless,
                icon=icon,
                visible=visible,
                show_when_ready=show_when_ready,
                init_scripts=init_scripts,
            )

        self.options = options
        native_visible = options.visible and not options.show_when_ready
        self._handle = _native.create_window(debug=options.debug, visible=native_visible)
        self._closed = False
        self._started = False
        self._visible = native_visible
        self._events: DefaultDict[str, list[EventHandler]] = defaultdict(list)
        self._bindings: dict[str, BindingHandler] = {}

        self.init(APPVERSE_BRIDGE_JS)
        if options.show_when_ready:
            self.init(READY_REVEAL_JS)
        self.bind("__appverse_message", self._receive_message)
        self.set_title(options.title)
        self.set_size(options.width, options.height, options.size_hint)

        for script in options.init_scripts:
            self.init(script)

        if options.frameless:
            self.set_frameless(True)
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

    def set_icon(self, icon: str | os.PathLike[str]) -> bool:
        icon_path = self._prepare_icon(icon)
        return bool(_native.set_icon(self._handle, str(icon_path)))

    def set_frameless(self, frameless: bool = True) -> bool:
        return bool(_native.set_frameless(self._handle, frameless))

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

    def set_html(self, html: str) -> None:
        _native.set_html(self._handle, html)

    def load_html(self, path: str | os.PathLike[str], *, encoding: str = "utf-8") -> None:
        html_path = Path(path).expanduser().resolve()
        self.set_html(html_path.read_text(encoding=encoding))

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

    def __enter__(self) -> "Window":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.destroy()


def create_window(**kwargs: Any) -> Window:
    return Window(**kwargs)


__all__ = [
    "APPVERSE_BRIDGE_JS",
    "CLOSE",
    "DESTROY",
    "ERROR",
    "HINT_FIXED",
    "HINT_MAX",
    "HINT_MIN",
    "HINT_NONE",
    "MESSAGE",
    "READY",
    "READY_REVEAL_JS",
    "READY_TO_SHOW",
    "SHOW",
    "HIDE",
    "START",
    "Window",
    "WindowOptions",
    "create_window",
]
