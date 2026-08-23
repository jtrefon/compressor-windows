# Compressor for Windows

Native Windows desktop UI (**WinUI 3, C++/WinRT**) for
[CompressionLib](https://github.com/jtrefon/compressionlib).

This repository is **UI only**: the engine is consumed as a pinned dependency
(`FetchContent`, exact tag — see `CMakeLists.txt`). All compression, archive,
format and integrity logic lives in the engine. No Qt, no third-party UI
framework — pure Windows App SDK (Fluent native look, MSIX-ready).

## Build (Windows, VS 2022+)

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --target winui_app contract_test
ctest -C Release --output-on-failure
```

The app is **unpackaged + self-contained** (`WindowsAppSDKSelfContained=true`):
a standalone executable with no runtime install required.

## Releasing

Push a `vX.Y.Z` tag: CI builds, runs the anti-corruption contract tests,
stages the standalone executable (+ Windows App SDK and VC runtimes), and
publishes a portable ZIP, an NSIS installer
(`CompressorWindows-vX.Y.Z-setup.exe`) and SHA-256 sums.

## Anti-corruption contract

`tests/contract_test.cpp` enforces the engine porting contract: byte-identical
round trips through every registry codec, loud rejection of corrupted streams,
and `.cza` archive create/list/verify. It runs on every build — a broken
release cannot be tagged.

## Engine pin policy

`COMPRESSIONLIB_TAG` in `CMakeLists.txt` pins the engine by exact tag. Bump it
deliberately, review the engine CHANGELOG, and let the contract test verify.
`v2.0.0` on the engine is reserved for the coordinated UI deployment release.

## Structure

- `app/` — WinUI 3 app (`App.xaml`, `MainWindow.xaml`, vcxproj; the vcxproj is
  driven by CMake so the engine is always built first)
- `tests/contract_test.cpp` — anti-corruption gate
- `.github/workflows/` — CI/CD (build-test + release with installer)