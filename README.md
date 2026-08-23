# Compressor for Windows

Qt 6 desktop UI for [CompressionLib](https://github.com/jtrefon/compressionlib).
This repository is **UI only**: the engine is consumed as a pinned dependency
(`FetchContent`, exact tag — see `CMakeLists.txt`). All compression, archive,
format and integrity logic lives in the engine.

## Build

```bat
python -m aqt install-qt windows desktop 6.7.2 win64_msvc2022_64 --outputdir %TEMP%\Qt
cmake -S . -B build -DCMAKE_PREFIX_PATH=%TEMP%\Qt\6.7.2\msvc2022_64
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## Releasing

Push a `vX.Y.Z` tag: CI builds, runs the anti-corruption contract tests,
stages a standalone executable (`windeployqt`), and publishes a portable ZIP,
an NSIS installer (`CompressorWindows-vX.Y.Z-setup.exe`) and SHA-256 sums.

## Anti-corruption contract

`tests/contract_test.cpp` enforces the engine porting contract: byte-identical
round trips through every registry codec, loud rejection of corrupted streams,
and `.cza` archive create/list/verify. It runs on every build — a broken
release cannot be tagged.

## Engine pin policy

`COMPRESSIONLIB_TAG` in `CMakeLists.txt` pins the engine by exact tag. Bump it
deliberately, review the engine CHANGELOG, and let the contract test verify.
`v2.0.0` on the engine is reserved for the coordinated UI deployment release.