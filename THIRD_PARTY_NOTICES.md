# Third-party notices

Snapback is licensed under the MIT License (see [LICENSE](LICENSE)). This file lists
third-party components compiled into or distributed with release packages, and their
licenses. Pin versions are recorded in [CMakeLists.txt](CMakeLists.txt),
[frontend/package-lock.json](frontend/package-lock.json), and
[third_party/onnxruntime-pins.json](third_party/onnxruntime-pins.json).

## Shipped in every desktop release

| Component | Version | Pin | License | How it is used |
| --- | --- | --- | --- | --- |
| nlohmann/json | 3.11.3 | `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03` | [MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT) | JSON parsing; compiled into `snapback_core` |
| webview | 0.12.0 | `3ab4b5d722438fc8a13e6ca830c5e2372d19a01d` | [MIT](https://github.com/webview/webview/blob/master/LICENSE) | Native webview host for the React UI |
| SQLite amalgamation | 3.45.3 | `sqlite-amalgamation-3450300.zip` (SHA-256 in CMakeLists.txt) | [Public domain](https://www.sqlite.org/copyright.html) | Local database engine |
| React | 19.2.8 | `frontend/package-lock.json` | [MIT](https://github.com/facebook/react/blob/main/LICENSE) | UI framework; bundled in `frontend/dist/` |
| React DOM | 19.2.8 | `frontend/package-lock.json` | [MIT](https://github.com/facebook/react/blob/main/LICENSE) | UI renderer; bundled in `frontend/dist/` |

## Shipped only when ONNX is enabled

Built with `-DSNAPBACK_ONNX=ON`. The prebuilt runtime is downloaded per
[third_party/onnxruntime-pins.json](third_party/onnxruntime-pins.json) and copied beside
the executable.

| Component | Version | License |
| --- | --- | --- |
| ONNX Runtime | 1.20.1 | [MIT](https://github.com/microsoft/onnxruntime/blob/main/LICENSE) |

## Build and test only (not in release packages)

| Component | Version | Pin | License | How it is used |
| --- | --- | --- | --- | --- |
| doctest | 2.4.11 | `ae7a13539fb71f270b87eb2e874fbac80bc8dda2` | [MIT](https://github.com/doctest/doctest/blob/master/LICENSE.txt) | C++ unit test framework |

## Frontend development dependencies

`npm ci` installs additional packages for typechecking, linting, testing, and bundling.
Those tools are not copied into `frontend/dist/` or release packages. The production
bundle contains only the runtime libraries listed above (React and React DOM).

To regenerate or audit the npm tree, run `npm ci` in `frontend/` and inspect
`package-lock.json`. Bump process for C++ dependencies is in
[docs/dependencies.md](docs/dependencies.md).
