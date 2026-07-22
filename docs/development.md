# Development

## Layout

```text
CMakeLists.txt              Native build configuration
src/native/                 C++ CPython extension
src/appverse/               Public Python package
src/pyverse/                Compatibility alias
examples/starter_app.py     Minimal desktop app
scripts/                    Windows helper scripts
docs/                       Project documentation
```

## Pull webview

```powershell
.\scripts\pull-webview.ps1
```

The script clones `webview/webview` tag `0.12.0` into `external/webview`. If Git
is unavailable, it downloads the tagged source archive.

## Build

```powershell
.\scripts\build.ps1
```

The build writes a development copy of `_native.pyd` into `src/appverse` and
installs runtime DLLs needed on Windows.

## Native ABI

The C++ extension uses CPython's stable ABI beginning at Python 3.9. Native state
is stored in a capsule and wrapped by the pure Python `appverse.Window` class.

## Roadmap

Near-term work:

- Python callable bindings from JavaScript.
- App lifecycle helpers.
- File and asset loading utilities.
- Packaging examples for distributable applications.
