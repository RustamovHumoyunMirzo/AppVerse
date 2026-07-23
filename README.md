# AppVerse

[![CI](https://github.com/RustamovHumoyunMirzo/AppVerse/actions/workflows/ci.yml/badge.svg)](https://github.com/RustamovHumoyunMirzo/AppVerse/actions/workflows/ci.yml)

AppVerse is a native app library for Python. It lets you open a
small native desktop window, render HTML/CSS/JavaScript, and keep application
logic in Python.

The native layer is powered by [`webview/webview`](https://github.com/webview/webview):
WebView2 on Windows, WKWebView on macOS, and WebKitGTK on Linux.

## Status

AppVerse is early-stage alpha. The first public API focuses on a production
packaging foundation and a minimal native window:

```python
import appverse

window = appverse.create_window(
    title="AppVerse",
    width=960,
    height=640,
    debug=True,
    show_when_ready=True,
    html="<h1>Hello from Python</h1>",
)

@window.bind("ping")
def ping(message):
    return {"reply": message}

window.run()
```

## Install

```bash
pip install appverse
```

Linux users also need WebKitGTK runtime libraries. See
[Platform Support](docs/platform-support.md).

Local HTML files can be loaded directly:

```python
window = appverse.create_window(show_when_ready=True)
window.load_html("ui/index.html")
window.run()
```

## Local Development

On Windows, install Python 3.9+, CMake, Ninja, Git, and a C++ compiler, then run:

```powershell
.\scripts\build.ps1
.\scripts\run-starter.ps1 -SkipBuild
```

The build script pulls `webview/webview` into `external/webview`, configures
CMake, and builds the native extension.

## Documentation

- [Getting Started](docs/getting-started.md)
- [API Reference](docs/api.md)
- [JavaScript Bridge](docs/js-bridge.md)
- [Platform Support](docs/platform-support.md)
- [Packaging and Publishing](docs/packaging.md)
- [Development](docs/development.md)
- [Changelog](CHANGELOG.md)

## Package Name

The PyPI distribution and public import are `appverse`. A small `pyverse`
compatibility alias is included for early local experiments.
