from pathlib import Path
import sys

REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

import appverse


HTML_FILE = REPO_ROOT / "examples" / "html" / "index.html"


def main() -> None:
    window = appverse.create_window(
        title="AppVerse Menubar Demo",
        width=1000,
        height=680,
        debug=False,
        devtools=False,
        show_when_ready=True,
        hardware_acceleration=True,
    )

    def log_menu(win: appverse.Window, item: dict) -> None:
        print(
            "menu:",
            item.get("key") or item["id"],
            "| label:",
            item["label"],
            "| checked:",
            item.get("checked"),
            "| enabled:",
            item.get("enabled"),
        )

    def new_file(win: appverse.Window, item: dict) -> None:
        print("New file selected")

    def toggle_statusbar(win: appverse.Window, item: dict) -> None:
        state = "on" if item.get("checked") else "off"
        print(f"Status bar toggled {state}")

    def set_theme(win: appverse.Window, item: dict) -> None:
        print(f"Theme changed to {item['label']}")

    def disable_open(win: appverse.Window, item: dict) -> None:
        applied = win.set_menu_item_enabled("open", False)
        print(f"Disabled Open item: {applied}")

    def rename_recent(win: appverse.Window, item: dict) -> None:
        applied = win.set_menu_item_label("recent_1", "Renamed Project")
        print(f"Renamed recent item: {applied}")

    menu_ids = window.set_menu(
        [
            {
                "label": "File",
                "children": [
                    {"id": "new", "label": "New", "handler": new_file},
                    {"id": "open", "label": "Open"},
                    "-",
                    {
                        "label": "Recent",
                        "children": [
                            {"id": "recent_1", "label": "Project 1"},
                            {"id": "recent_2", "label": "Project 2", "enabled": False},
                        ],
                    },
                    "-",
                    {"id": "disable_open", "label": "Disable Open", "handler": disable_open},
                    {"id": "rename_recent", "label": "Rename Recent Item", "handler": rename_recent},
                ],
            },
            {
                "label": "View",
                "children": [
                    {
                        "id": "show_statusbar",
                        "label": "Show Status Bar",
                        "type": "checkbox",
                        "checked": True,
                        "handler": toggle_statusbar,
                    },
                    "-",
                    {
                        "id": "theme_light",
                        "label": "Light Theme",
                        "type": "radio",
                        "group": "theme",
                        "checked": True,
                        "handler": set_theme,
                    },
                    {
                        "id": "theme_dark",
                        "label": "Dark Theme",
                        "type": "radio",
                        "group": "theme",
                        "handler": set_theme,
                    },
                    {
                        "id": "theme_system",
                        "label": "System Theme",
                        "type": "radio",
                        "group": "theme",
                        "handler": set_theme,
                    },
                ],
            },
            {
                "label": "Window",
                "children": [
                    {"id": "always_on_top", "label": "Always On Top", "type": "checkbox"},
                    {"id": "devtools", "label": "Enable DevTools", "type": "checkbox"},
                ],
            },
            {
                "label": "Help",
                "children": [
                    {"id": "about", "label": "About AppVerse"},
                ],
            },
        ]
    )

    # Manual API still works alongside set_menu().
    window.add_submenu("Help", "Links")
    window.add_menu_item("Help/Links", "Documentation", key="docs")
    window.add_check_menu_item("View", "Manual Checkbox", key="manual_check", checked=False)
    window.add_radio_menu_item("View", "Manual Radio A", "manual", key="manual_a", checked=True)
    window.add_radio_menu_item("View", "Manual Radio B", "manual", key="manual_b")

    @window.on(appverse.MENU)
    def on_any_menu(win: appverse.Window, item: dict) -> None:
        log_menu(win, item)

    @window.on_menu("always_on_top")
    def on_always_on_top(win: appverse.Window, item: dict) -> None:
        win.set_always_on_top(bool(item.get("checked")))

    @window.on_menu("devtools")
    def on_devtools(win: appverse.Window, item: dict) -> None:
        win.set_devtools_enabled(bool(item.get("checked")))

    @window.on_menu("about")
    def on_about(win: appverse.Window, item: dict) -> None:
        print("AppVerse native menubar demo")

    @window.on_menu("docs")
    def on_docs(win: appverse.Window, item: dict) -> None:
        print("Documentation menu clicked")

    print("Menu ids:", menu_ids)

    window.load_html(HTML_FILE)
    window.run()


if __name__ == "__main__":
    main()
