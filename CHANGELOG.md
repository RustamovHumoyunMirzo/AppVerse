# Changelog

All notable changes to AppVerse will be documented in this file.

The project follows semantic versioning while the public API matures.

## [1.0.0a0] - 2026-07-22

### Added

- Initial `appverse` Python package scaffold.
- Native WebView-backed `Window` API for title, size, HTML, navigation,
  JavaScript initialization, JavaScript evaluation, event loop, and destroy.
- `WindowOptions` for title, size, content, debug, icon, frameless, and init
  script configuration.
- Window event emitter with `ready`, `close`, `destroy`, and `message` events.
- Added `start`, `show`, and `hide` window events.
- JavaScript bridge for Python-to-JavaScript events and JavaScript-to-Python
  function calls.
- Native `terminate`, `bind`, `unbind`, `dispatch_eval`, Windows icon, and
  Windows frameless APIs.
- Cross-platform `show_when_ready` option, `READY_TO_SHOW` event, native `show`
  and `hide` visibility control, and `load_html` for local HTML files.
- Fixed `show_when_ready` startup flicker by avoiding internal native
  `show()`/`hide()` calls during construction.
- Added a native dark preload document to reduce the initial white webview flash
  before Python loads the app HTML.
- Added `visible` window option for hidden startup. `show_when_ready=True` now
  starts native-hidden, shows at `run()`, and reveals page content after document
  readiness.
- Added position, min/max/fixed size helpers, fullscreen, minimize, maximize,
  restore, and devtools option.
- Documented Electron-style app regions as app-authored CSS.
- Added native app-region drag support through CSS detection and `start_drag()`.
- Added right-click native system menu support for Windows app-region drag
  areas.
- Added app-region double-click maximize/restore behavior through native
  `toggle_maximize()`.
- Expanded native window APIs across platforms: macOS app icon, frameless,
  position, fullscreen, and restore behavior; GTK3 icon and frameless support;
  explicit Win32 fullscreen state restore and system-menu state syncing.
- `load_html()` now uses `file://` navigation so local relative links resolve,
  and native titles inherit the document `<title>` when no title is set.
- Improved icon path handling and added optional Pillow-based image-to-ICO
  conversion through `appverse[icons]`.
- Native CPython wheel build configuration for Python 3.9+.
- PowerShell scripts to pull `webview/webview`, build locally, and run the
  starter app.
- GitHub Actions workflows for CI, wheel builds, and PyPI publishing.
- Documentation for setup, API, platform support, packaging, and development.
