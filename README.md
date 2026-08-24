# Compressor for Windows

Native Windows desktop UI (**WinUI 3, C++/WinRT**) for
[CompressionLib](https://github.com/jtrefon/compressionlib).

This repository is **UI only**: the engine is consumed as a pinned dependency
(`FetchContent`, exact tag — see `CMakeLists.txt`). All compression, archive,
format and integrity logic lives in the engine. No Qt, no third-party UI
framework — pure Windows App SDK (Fluent native look).

## Features

- Compress / decompress any file with the strategy and thread count of your
  choice (strategy list generated from `CodecRegistry`)
- **Live progress**: progress bar plus a live status line showing percent,
  bytes in/out and the running compression ratio; CRC verification surfaced
  in the UI; **Cancel** aborts the operation and removes the partial output
- **Drag & drop**: drop files anywhere on the window to load them; drop
  multiple files to bundle them into an archive
- Native file pickers (open / save with `.cpz` default), archive picker and
  folder picker
- `.cza` archives: create from multiple files, open & verify every block,
  list entries, extract a selected entry (path-traversal safe)
- **File association**: the installer registers `.cpz`; double-clicking an
  archive opens the app with it loaded, ready to decompress
- Unpackaged + self-contained (`WindowsAppSDKSelfContained=true`): standalone
  executable, no runtime install required
- App icon, per-monitor DPI aware, Fluent theme (system dark/light)

### Updates

The app checks for updates against the GitHub Releases API directly - the
repository is the update server, no dedicated infrastructure. A quiet check
runs on launch; "Check for updates..." is available in the UI. When a newer
release exists, a dialog shows the release notes with a Download button
(pointing at the `-setup.exe` asset).

**Version sync rule**: `kAppVersion` in `app/UpdateService.h` must equal the
next release tag (the updater compares against it). The CI `--version` gate
keeps the output format valid; the release tag is the source of truth.

### Code signing

The release pipeline signs the executable and installer via **SignPath
Foundation** (free OSS code signing) when `SIGNPATH_API_TOKEN` and
`SIGNPATH_ORG_ID` secrets are configured. See [docs/SIGNING.md](docs/SIGNING.md)
for setup. Falls back to a PFX certificate if SignPath is not configured.

Without signing, the build still produces and publishes unsigned artifacts.

### Windows SmartScreen / Smart App Control

Current releases are **unsigned** (pending SignPath Foundation approval). On
Windows 11 with Smart App Control enabled, you will see a blue "Windows
protected your PC" screen with only a **Don't run** button.

**Quick workaround:** right-click the `.exe` → Properties → check **Unblock**
→ Apply → double-click to run. Or use the portable ZIP instead of the installer.

**Permanent fix:** once SignPath is approved, all releases are signed with a
real OV certificate. The hard SAC block disappears, and users get the standard
"More info → Run anyway" path. Reputation builds over time and warnings fade.

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

## End-to-end tests (CI)

Three test layers run against the **real executable**:

1. **Headless round trip** — `CompressorWindows.exe compress <in> <out>
   [codec]` / `decompress <in> <out>` runs the engine facade directly (no UI).
   CI compresses and decompresses a random payload and byte-compares the
   result — on every build and again against the staged portable executable
   in the release pipeline.
2. **UI integration (--uitest)** — the real window is spawned and activated
   (XAML loads, controls instantiate, strategy combo populated from the
   registry), then the real controls and click handlers are driven exactly as
   a user would, with the XAML message loop pumping until the UI reports
   completion:
   `CompressorWindows.exe --uitest <compress|decompress> --in <path> --out
   <path> --result <file> [--codec <name>]`
   The result is written to `--result` ("OK ..." or "FAIL ...") since XAML
   teardown makes process exit codes unreliable. CI runs a full
   compress + decompress workflow through the live UI on every build, and
   again against the **installed** executable after the installer runs.
3. **Installer verification** (release pipeline): the NSIS installer is run
   silently to a clean directory, the installed exe + uninstaller are checked,
   the installed app is launched (must not crash), the `--uitest` workflow is
   executed through the installed app, then it is uninstalled and the install
   directory must be gone.

## Engine pin policy

`COMPRESSIONLIB_TAG` in `CMakeLists.txt` pins the engine by exact tag
(enforced at configure time). Bump it deliberately, review the engine
CHANGELOG, and let the contract test verify. Engine versioning is purely
semantic: `v2.0.0` comes only when the engine breaks compatibility — this app
versions independently on its own line.

## Structure

- `app/` — WinUI 3 app (`App.xaml`, `MainWindow.xaml`, vcxproj; the vcxproj is
  driven by CMake so the engine is always built first)
- `tests/contract_test.cpp` — anti-corruption gate
- `.github/workflows/` — CI/CD (build-test + release with installer)