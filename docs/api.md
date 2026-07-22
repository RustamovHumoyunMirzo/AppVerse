# API Reference

## `appverse.create_window`

```python
appverse.create_window(*, debug: bool = False) -> appverse.Window
```

Creates a native webview window. Enable `debug=True` during development to expose
the platform webview's developer tooling when available.

## `appverse.Window`

### `set_title(title: str) -> None`

Sets the native window title.

### `set_size(width: int, height: int, hint: int = HINT_NONE) -> None`

Sets the window size. Size hints are:

- `HINT_NONE`
- `HINT_MIN`
- `HINT_MAX`
- `HINT_FIXED`

### `set_html(html: str) -> None`

Renders an HTML document string.

### `navigate(url: str) -> None`

Navigates the window to a URL.

### `init(javascript: str) -> None`

Injects JavaScript before page scripts run.

### `eval(javascript: str) -> None`

Evaluates JavaScript in the current page.

### `run() -> None`

Runs the native event loop. This call blocks until the window exits.

### `destroy() -> None`

Destroys the native window handle.
