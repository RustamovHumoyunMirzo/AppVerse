# JavaScript Bridge

AppVerse injects `window.appverse` into pages loaded with `set_html` or
`navigate`.

## JavaScript to Python

Register a Python function:

```python
window = appverse.create_window()

@window.bind("lookup_user")
def lookup_user(user_id: int) -> dict[str, object]:
    return {"id": user_id, "name": "Ada"}

window.load_html("ui/index.html")
window.run()
```

Register bindings before loading HTML when page scripts or early user actions
depend on them.

Call it from JavaScript:

```javascript
const user = await window.appverse.call("lookup_user", 7);
```

The arguments and return value must be JSON-serializable.

## JavaScript Events to Python

JavaScript:

```javascript
await window.appverse.send("form-submit", { email: "ada@example.com" });
```

Python:

```python
@window.on(appverse.MESSAGE)
def handle_message(name, detail):
    print(name, detail)
```

## Python to JavaScript

Python:

```python
window.send("user-loaded", {"id": 7})
```

JavaScript:

```javascript
window.appverse.on("user-loaded", (event) => {
  console.log(event.detail.id);
});
```

## Direct Evaluation

Use `eval` for immediate script evaluation and `dispatch_eval` when calling from
threads or callbacks that should hop onto the native UI loop.

```python
window.dispatch_eval("document.body.dataset.ready = 'true'")
```

## Ready To Show

Use `show_when_ready=True` to keep the document visually hidden while the initial
HTML loads. AppVerse reveals the document after `DOMContentLoaded` and emits
`appverse.READY_TO_SHOW`. The option creates the native window hidden, then shows
it at `run()` after content and scripts have been installed.

```python
window = appverse.create_window(show_when_ready=True)

@window.on(appverse.READY_TO_SHOW)
def visible(_window):
    print("window revealed")
```
