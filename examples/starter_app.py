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
    <button id="ping">Call Python</button>
    <span id="status">Ready</span>
  </main>
  <script>
    window.appverse.on("python:tick", (event) => {
      document.querySelector("#status").textContent = event.detail.message;
    });

    document.querySelector("#ping").addEventListener("click", async () => {
      const result = await window.appverse.call("ping", new Date().toLocaleTimeString());
      document.querySelector("#status").textContent = result.message;
      await window.appverse.send("button-clicked", result);
    });
  </script>
</body>
</html>
"""


def main() -> None:
    window = appverse.create_window(
        title="AppVerse Starter",
        width=960,
        height=640,
        debug=True,
        show_when_ready=True,
        html=HTML,
    )

    @window.on(appverse.MESSAGE)
    def _message(name: str, detail: object) -> None:
        print(f"JS event: {name} {detail}")

    @window.bind("ping")
    def _ping(timestamp: str) -> dict[str, str]:
        message = f"Python received a click at {timestamp}"
        window.send("python:tick", {"message": message})
        return {"message": message}

    window.show()
    window.run()


if __name__ == "__main__":
    main()
