# API Reference

## `appverse.create_window`

```python
appverse.create_window(**options) -> appverse.Window
```

Creates a native webview window. Accepted options mirror `WindowOptions`:

- `title`
- `width`
- `height`
- `size_hint`
- `debug`
- `html`
- `url`
- `frameless`
- `icon`
- `visible`
- `show_when_ready`
- `init_scripts`

## `appverse.WindowOptions`

```python
appverse.WindowOptions(
    title="AppVerse",
    width=960,
    height=640,
    size_hint=appverse.HINT_NONE,
    debug=False,
    html=None,
    url=None,
    frameless=False,
    icon=None,
    visible=True,
    show_when_ready=False,
    init_scripts=(),
)
```

Use `WindowOptions` when a window configuration is shared across your app.

## Events

```python
window.on("ready", handler)
window.once("destroy", handler)
window.off("message", handler)
window.emit("custom", payload)
```

Built-in event names:

- `START`: emitted when `run()` begins.
- `READY`: emitted before the native run loop starts.
- `CLOSE`: emitted before `terminate()`.
- `SHOW`: emitted after `show()` is requested. Handlers receive
  `(window, applied)`.
- `HIDE`: emitted after `hide()` is requested. Handlers receive
  `(window, applied)`.
- `DESTROY`: emitted after the run loop exits or when `destroy()` is called.
- `MESSAGE`: emitted when JavaScript calls `window.appverse.send(...)`.
- `READY_TO_SHOW`: emitted when the document is ready and AppVerse reveals the
  page content.
- `ERROR`: reserved for framework error events.

## JavaScript Bridge

Python registers JavaScript-callable functions with `bind`:

```python
@window.bind("add")
def add(left: int, right: int) -> int:
    return left + right
```

JavaScript calls Python through `window.appverse.call`:

```javascript
const value = await window.appverse.call("add", 2, 3);
```

JavaScript sends events to Python:

```javascript
await window.appverse.send("saved", { id: 42 });
```

Python sends events to JavaScript:

```python
window.send("saved", {"id": 42})
```

JavaScript receives Python events:

```javascript
window.appverse.on("saved", (event) => {
  console.log(event.detail.id);
});
```

## `appverse.Window`

### `set_title(title: str) -> None`

Sets the native window title.

### `set_size(width: int, height: int, hint: int = HINT_NONE) -> None`

Sets the window size. Size hints are:

- `HINT_NONE`
- `HINT_MIN`
- `HINT_MAX`
- `HINT_FIXED`

### `set_icon(icon: str | PathLike) -> bool`

Attempts to set the native window icon. Returns `True` when supported and
applied. Relative paths are resolved from the current working directory. Windows
uses `.ico` directly and can convert common image formats when Pillow is
installed with `appverse[icons]`.

### `set_frameless(frameless: bool = True) -> bool`

Attempts to toggle native frame decorations. Returns `True` when supported and
applied. The current native implementation supports Windows.

### `set_html(html: str) -> None`

Renders an HTML document string.

### `load_html(path: str | PathLike, *, encoding: str = "utf-8") -> None`

Reads a local HTML file and renders it with `set_html`.

### `navigate(url: str) -> None`

Navigates the window to a URL.

### `init(javascript: str) -> None`

Injects JavaScript before page scripts run.

### `eval(javascript: str) -> None`

Evaluates JavaScript in the current page.

### `dispatch_eval(javascript: str) -> None`

Dispatches JavaScript evaluation onto the native UI loop.

### `bind(name: str, handler: Callable) -> Callable`

Binds a Python callable as `window.<name>(...)` and
`window.appverse.call(name, ...)` in JavaScript. Arguments and return values must
be JSON-serializable.

### `unbind(name: str) -> None`

Removes a JavaScript binding.

### `send(event: str, detail: Any = None) -> None`

Sends an event from Python to JavaScript through `window.appverse`.

### `show() -> bool`

Shows the native window. Use this for explicit visibility control after the
window exists. Emits `SHOW`.

### `hide() -> bool`

Hides the native window. `show_when_ready` uses construction-time native hidden
startup and a preload reveal script to avoid early native-window flicker. Emits
`HIDE`.

## Hidden Startup

Use `visible=False` when you want a window to stay hidden through `run()` until
you explicitly call `show()`:

```python
window = appverse.create_window(visible=False, html="<h1>Hidden</h1>")
window.run()
```

Use `show_when_ready=True` when you want AppVerse to create the native window
hidden, show it when `run()` starts, and reveal the page content after the
document is ready. This avoids waiting for browser readiness while the native
window is still hidden.

### `terminate() -> None`

Requests the native event loop to exit.

### `run() -> None`

Runs the native event loop. This call blocks until the window exits.

### `destroy() -> None`

Destroys the native window handle.
