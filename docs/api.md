# API Reference

## `appverse.create_window`

```python
appverse.create_window(**options) -> appverse.Window
```

Creates a native webview window. Accepted options mirror `WindowOptions`:

- `title`
- `width`
- `height`
- `x`
- `y`
- `min_width`
- `min_height`
- `max_width`
- `max_height`
- `size_hint`
- `debug`
- `devtools`
- `html`
- `url`
- `frameless`
- `fullscreen`
- `icon`
- `visible`
- `show_when_ready`
- `init_scripts`

## `appverse.WindowOptions`

```python
appverse.WindowOptions(
    title=None,
    width=960,
    height=640,
    x=None,
    y=None,
    min_width=None,
    min_height=None,
    max_width=None,
    max_height=None,
    size_hint=appverse.HINT_NONE,
    debug=False,
    devtools=False,
    html=None,
    url=None,
    frameless=False,
    fullscreen=False,
    icon=None,
    visible=True,
    show_when_ready=False,
    init_scripts=(),
)
```

Use `WindowOptions` when a window configuration is shared across your app.
When `title` is `None`, AppVerse inherits the first `<title>` from HTML loaded
with `set_html()` or `load_html()`.

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

Backdrop effect names:

- `BACKDROP_NONE`
- `BACKDROP_ACRYLIC`
- `BACKDROP_MICA`
- `BACKDROP_GLASS`

### `set_min_size(width: int, height: int) -> None`

Sets minimum resize bounds.

### `set_max_size(width: int, height: int) -> None`

Sets maximum resize bounds.

### `set_fixed_size(width: int, height: int) -> None`

Sets a fixed, non-resizable size.

### `set_position(x: int, y: int) -> bool`

Attempts to move the native window to screen coordinates.

### `set_icon(icon: str | PathLike) -> bool`

Attempts to set the native window icon. Returns `True` when supported and
applied. Relative paths are resolved from the current working directory. Windows
uses `.ico` directly and can convert common image formats when Pillow is
installed with `appverse[icons]`. macOS applies the image as the application
icon. GTK3 Linux builds apply the icon through GTK.

### `set_frameless(frameless: bool = True) -> bool`

Attempts to toggle native frame decorations. Returns `True` when supported and
applied. The native implementation supports Windows, macOS, and GTK Linux.

### `set_backdrop_effect(effect: str) -> bool`

Applies a native translucent backdrop where supported. Accepted values are
`"none"`, `"acrylic"`, `"mica"`, and `"glass"`; the same strings are exported as
`BACKDROP_NONE`, `BACKDROP_ACRYLIC`, `BACKDROP_MICA`, and `BACKDROP_GLASS`.

Platform mapping:

- Windows: DWM system backdrop types (`none`, transient/acrylic, main/mica, tabbed/glass).
- macOS: `NSVisualEffectView` materials approximating popover, window background, and ultra-thin glass.
- Linux: compositor-friendly GTK opacity fallback. Real blur depends on the Wayland compositor and is not universally controllable from GTK.

### `set_fullscreen(fullscreen: bool = True) -> bool`

Toggles fullscreen/maximized presentation where supported.

### `minimize() -> bool`

Minimizes the native window.

### `maximize() -> bool`

Maximizes the native window.

### `restore() -> bool`

Restores a minimized, maximized, or fullscreen window where supported.

### `toggle_maximize() -> bool`

Toggles between maximized and restored states where supported.

### `start_drag() -> bool`

Starts a native window drag operation. AppVerse calls this automatically when
the user presses an element styled with app-region drag CSS.

### `show_window_menu(x: int, y: int) -> bool`

Shows the native window controls context menu at screen coordinates. AppVerse
calls this automatically when the user right-clicks an app-region drag area.
Windows currently shows the OS system menu; unsupported platforms return
`False`.

### `open_devtools() -> bool`

Returns whether developer tooling was requested. Enable it with `debug=True` or
`devtools=True` when creating the window.

## App Region CSS

App regions are authored in HTML/CSS, like Electron. Use them with frameless
windows. AppVerse detects these CSS properties and starts native window drag on
left-click. Double-clicking a drag region toggles maximize/restore where
supported. On Windows, right-clicking a drag region opens the native system
window menu.

```css
.titlebar {
  -webkit-app-region: drag;
  app-region: drag;
}

button,
input,
a,
.no-drag {
  -webkit-app-region: no-drag;
  app-region: no-drag;
}
```


### `set_html(html: str) -> None`

Renders an HTML document string.

### `load_html(path: str | PathLike, *, encoding: str = "utf-8") -> None`

Loads a local HTML file through a `file://` URL so relative CSS, scripts, images,
and links resolve from that file's directory. If `title` was not set explicitly,
the first `<title>` in the file becomes the native window title.

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
