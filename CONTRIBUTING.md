# Contributing

Thanks for helping build AppVerse.

## Development Setup

Install Python 3.9+, CMake 3.26+, Ninja, Git, and a platform C++ toolchain.

Windows:

```powershell
.\scripts\build.ps1
.\scripts\run-starter.ps1 -SkipBuild
```

Linux:

```bash
sudo apt-get install -y cmake ninja-build libgtk-3-dev libwebkit2gtk-4.1-dev
python -m pip install --upgrade pip build
python -m build
```

macOS:

```bash
python -m pip install --upgrade pip build
python -m build
```

## Pull Requests

Before opening a PR:

- Keep changes focused and documented.
- Update `docs/` and `CHANGELOG.md` for user-visible behavior.
- Run a local package build where your platform allows it.
- Include screenshots or a short screen recording for UI-facing changes.

## Native Code Guidelines

- Keep the Python ABI stable unless there is a clear release reason not to.
- Prefer `webview/webview` APIs over platform-specific branches.
- Keep the Python wrapper small and predictable.
- Do not add new native dependencies without documenting wheel impact.

## Release Process

Releases are published under the `appverse` PyPI project. GitHub Actions builds
wheels with cibuildwheel and publishes through PyPI trusted publishing.

1. Update the version in `pyproject.toml` and `CMakeLists.txt`.
2. Update `CHANGELOG.md`.
3. Create a GitHub release or run the publish workflow manually.
