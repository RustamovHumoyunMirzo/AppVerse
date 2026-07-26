from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

import appverse


def main() -> None:
    window = appverse.create_window(
        width=960,
        height=640,
        debug=True,
        devtools=False,
    )

    window.load_html(r"examples\html\index.html")
    window.run()


if __name__ == "__main__":
    main()
