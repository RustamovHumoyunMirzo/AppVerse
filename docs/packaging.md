# Packaging and Publishing

AppVerse uses:

- `scikit-build-core` as the PEP 517 build backend.
- CMake for native compilation.
- `cibuildwheel` for platform wheels.
- PyPI trusted publishing through GitHub Actions.

## Build Locally

```bash
python -m pip install --upgrade pip build
python -m build
```

Artifacts are written to `dist/`.

## Build Wheels in CI

The `Wheels` workflow builds CPython stable ABI wheels for:

- Windows: x64, x86, ARM64
- macOS: x86_64, arm64
- Linux: x86_64, aarch64

Linux wheels depend on WebKitGTK at runtime. The workflow installs development
headers during wheel builds and excludes system WebKitGTK libraries from wheel
repair so the OS package manager remains responsible for them.

## Publish to PyPI

1. Configure the `appverse` PyPI project for trusted publishing from this GitHub
   repository.
2. Create a GitHub environment named `pypi`.
3. Publish a GitHub release or run the `Publish` workflow manually.

No PyPI API token is required when trusted publishing is configured.

## Privacy Notes

The package metadata intentionally avoids personal names, email addresses, and
personal repository URLs. If you want to keep your identity private, publish from
a GitHub organization and PyPI account/organization name that you are comfortable
showing publicly.

The publish workflow disables PyPI attestations because attestations can expose
the GitHub workflow identity used to publish the package. Trusted publishing
still avoids long-lived PyPI API tokens.
