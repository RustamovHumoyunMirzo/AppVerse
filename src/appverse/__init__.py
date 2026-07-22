from __future__ import annotations

from pathlib import Path
import os
from typing import Final


if os.name == "nt" and hasattr(os, "add_dll_directory"):
    os.add_dll_directory(str(Path(__file__).resolve().parent))

from . import _native


HINT_NONE: Final[int] = _native.HINT_NONE
HINT_MIN: Final[int] = _native.HINT_MIN
HINT_MAX: Final[int] = _native.HINT_MAX
HINT_FIXED: Final[int] = _native.HINT_FIXED


class Window:
    """Native webview-backed application window."""

    def __init__(self, *, debug: bool = False) -> None:
        self._handle = _native.create_window(debug=debug)
        self._closed = False

    def set_title(self, title: str) -> None:
        _native.set_title(self._handle, title)

    def set_size(self, width: int, height: int, hint: int = HINT_NONE) -> None:
        _native.set_size(self._handle, width, height, hint)

    def set_html(self, html: str) -> None:
        _native.set_html(self._handle, html)

    def navigate(self, url: str) -> None:
        _native.navigate(self._handle, url)

    def init(self, javascript: str) -> None:
        _native.init(self._handle, javascript)

    def eval(self, javascript: str) -> None:
        _native.eval(self._handle, javascript)

    def run(self) -> None:
        _native.run(self._handle)

    def destroy(self) -> None:
        if not self._closed:
            _native.destroy(self._handle)
            self._closed = True

    def __enter__(self) -> "Window":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.destroy()


def create_window(*, debug: bool = False) -> Window:
    return Window(debug=debug)


__all__ = [
    "HINT_FIXED",
    "HINT_MAX",
    "HINT_MIN",
    "HINT_NONE",
    "Window",
    "create_window",
]
