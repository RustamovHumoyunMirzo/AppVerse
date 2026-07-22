from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

import appverse


HTML = """
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>AppVerse Starter</title>
  <style>
    :root {
      color-scheme: light dark;
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #101418;
      color: #f7fafc;
    }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      background:
        radial-gradient(circle at 15% 20%, rgba(33, 150, 243, 0.28), transparent 28rem),
        linear-gradient(135deg, #101418 0%, #182028 50%, #151a1f 100%);
    }
    main {
      width: min(720px, calc(100vw - 40px));
    }
    h1 {
      margin: 0 0 12px;
      font-size: clamp(34px, 7vw, 76px);
      line-height: 0.94;
      letter-spacing: 0;
    }
    p {
      max-width: 58ch;
      margin: 0 0 24px;
      color: #cbd5df;
      font-size: 18px;
      line-height: 1.6;
    }
    button {
      border: 0;
      border-radius: 8px;
      padding: 12px 16px;
      background: #41d19f;
      color: #07110d;
      font: inherit;
      font-weight: 700;
      cursor: pointer;
    }
    #status {
      display: inline-block;
      margin-left: 12px;
      color: #9fb0bf;
    }
  </style>
</head>
<body>
  <main>
    <h1>AppVerse is alive.</h1>
    <p>
      This window is Python driving native WebView2 through a tiny C++ bridge.
      Next up: Python-to-JavaScript bindings, packaging, and app lifecycle APIs.
    </p>
    <button id="ping">Run JavaScript</button>
    <span id="status">Ready</span>
  </main>
  <script>
    document.querySelector("#ping").addEventListener("click", () => {
      document.querySelector("#status").textContent = `Clicked at ${new Date().toLocaleTimeString()}`;
    });
  </script>
</body>
</html>
"""


def main() -> None:
    window = appverse.create_window(debug=True)
    window.set_title("AppVerse Starter")
    window.set_size(960, 640, appverse.HINT_NONE)
    window.set_html(HTML)
    window.run()


if __name__ == "__main__":
    main()
