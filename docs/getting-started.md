# Getting Started

AppVerse opens a native desktop webview from Python.

## Install

```bash
pip install appverse
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

window = appverse.create_window(debug=True)
window.set_title("AppVerse Starter")
window.set_size(960, 640)
window.set_html(html)
window.run()
```

## Repository Example

Run:

```powershell
python examples\starter_app.py
```

The example adds this repository's `src` directory to `sys.path`, so it can run
before the package is installed.
