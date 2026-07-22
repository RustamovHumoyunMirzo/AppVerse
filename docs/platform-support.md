# Platform Support

AppVerse uses `webview/webview`, so each operating system uses the browser
engine provided by the platform.

## Windows

Backend: Microsoft Edge WebView2.

Most current Windows installations already include the WebView2 runtime. If the
runtime is missing, install the Microsoft Edge WebView2 Runtime.

Wheels include `WebView2Loader.dll`.

## macOS

Backend: WKWebView.

No additional runtime is normally required.

## Linux

Backend: WebKitGTK.

Install runtime libraries for your distribution before running AppVerse apps.

Ubuntu/Debian:

```bash
sudo apt-get install -y libgtk-3-0 libwebkit2gtk-4.1-0
```

Development builds also need headers:

```bash
sudo apt-get install -y libgtk-3-dev libwebkit2gtk-4.1-dev
```

Some distributions still package WebKitGTK as `4.0` instead of `4.1`; install
the matching package for your OS.

## Wheel Strategy

The Python extension targets the CPython stable ABI for Python 3.9+, so each
platform and architecture can ship one wheel for all supported CPython minors.
Native wheels are still platform-specific because they contain compiled code.
