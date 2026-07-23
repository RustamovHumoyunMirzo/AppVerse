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
- JavaScript bridge for Python-to-JavaScript events and JavaScript-to-Python
  function calls.
- Native `terminate`, `bind`, `unbind`, `dispatch_eval`, Windows icon, and
  Windows frameless APIs.
- Cross-platform `show_when_ready` option, `READY_TO_SHOW` event, native `show`
  and `hide` visibility control, and `load_html` for local HTML files.
- Native CPython wheel build configuration for Python 3.9+.
- PowerShell scripts to pull `webview/webview`, build locally, and run the
  starter app.
- GitHub Actions workflows for CI, wheel builds, and PyPI publishing.
- Documentation for setup, API, platform support, packaging, and development.
