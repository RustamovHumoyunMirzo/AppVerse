# Changelog

All notable changes to AppVerse will be documented in this file.

The project follows semantic versioning.

## [1.0.0] - 2026-07-26

### Added

- Added `set_background_color()` with hex, RGBA, transparent, and semantic
  color support.
- Added `set_border_color()` for DWM border colors on Windows and a GTK border
  fallback on Linux.
- Added native window state getters and a `fullscreenable` window option for
  controlling fullscreen entry.
- Added `set_window_captions()` for native caption controls and titlebar color
  customization where the platform exposes real OS controls.
- Added `set_shadow()` and `has_shadow()`/`hasShadow` for native window shadow
  control, with Linux-only named shadow style presets.
- Added `get_handle()` for retrieving the native platform window handle as an
  integer pointer.
- Added native devtools toggling with WebView2 accelerator-key blocking when
  devtools are disabled.
- Added hardware-acceleration preferences for GPU/WebGL-oriented webview
  performance tuning across supported backends.

### Changed

- Promoted AppVerse from `1.0.0a0` to the stable `1.0.0` package release.
- Updated package metadata and README status for the production-ready release.

### Fixed

- Fixed Linux wheel builds by avoiding backdrop enum names that collide with
  platform macros.

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
- Added `set_backdrop_effect()` with AppVerse backdrop constants for Windows
  DWM backdrops, macOS visual effects, and a Linux GTK translucency fallback.
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
