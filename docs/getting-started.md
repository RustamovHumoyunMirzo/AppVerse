# Getting Started

AppVerse opens a native desktop webview from Python.

## Install

```bash
pip install appverse
```

For PNG/JPG icon conversion on Windows:

```bash
pip install "appverse[icons]"
```

For local development from this repository:

```powershell
.\scripts\build.ps1
.\scripts\run-starter.ps1 -SkipBuild
```

## First Window

```python
import appverse

html = """
<!doctype html>
<html>
  <body>
    <h1>Hello from AppVerse</h1>
    <button onclick="document.body.append(' clicked')">Click</button>
  </body>
</html>
"""

window = appverse.create_window(
    title="AppVerse Starter",
    width=960,
    height=640,
    debug=True,
    show_when_ready=True,
    html=html,
)
window.run()
```

## Load Local HTML

```python
window = appverse.create_window(title="Local UI", show_when_ready=True)
window.load_html("ui/index.html")
window.run()
```

To keep a window hidden until you decide to show it:

```python
window = appverse.create_window(visible=False)
window.load_html("ui/index.html")
window.run()
```

## Events and Bridge

```python
@window.on(appverse.READY)
def ready(_window):
    window.send("python-ready", {"ok": True})

@window.on(appverse.SHOW)
def shown(_window, applied):
    print("show requested", applied)

@window.bind("ping")
def ping(message):
    return {"reply": message}

window.set_html(html)
window.run()
```

```javascript
window.appverse.on("python-ready", (event) => console.log(event.detail));
const result = await window.appverse.call("ping", "hello");
```

## Repository Example

Run:

```powershell
python examples\starter_app.py
```

The example adds this repository's `src` directory to `sys.path`, so it can run
before the package is installed.
