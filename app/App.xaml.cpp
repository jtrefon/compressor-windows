#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <compression/app/ArchiveService.hpp>
#include "UpdateService.h"
#include <compression/app/CompressionService.hpp>
#include <compression/codec/CodecRegistry.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <shellapi.h>
#include <string>
#include <thread>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {

using namespace compression;

// Headless automation mode for the real executable (used by CI end-to-end
// tests): CompressorWindows.exe compress <in> <out> [codec]
//                              decompress <in> <out>
// Runs the same facade the GUI uses; results go to stdout, exit code signals
// success/failure so tests can assert on it.
int RunHeadless(const std::vector<std::wstring> &args) {
  if (!args.empty() && args[0] == L"--version") {
    wprintf(L"%ls\n", updates::kAppVersion);
    fflush(stdout);
    return 0;
  }
  if (args.empty()) {
    fprintf(stderr,
            "usage: CompressorWindows compress <in> <out> [codec]\n"
            "                 decompress <in> <out>\n"
            "                 archive create <out.cza> <file> [file...]\n"
            "                 archive verify <in.cza>\n");
    return 2;
  }
  try {
    compression::CompressionService service;
    if (args[0] == L"compress") {
      if (args.size() < 3 || args.size() > 4) {
        fprintf(stderr, "usage: compress <in> <out> [codec]\n");
        return 2;
      }
      compression::CompressionOptions options;
      if (args.size() == 4) {
        const std::string codecName(args[3].begin(), args[3].end());
        options.codec = compression::codec::CodecRegistry::instance().idOf(codecName);
        if (options.codec == compression::format::AlgorithmID::UNKNOWN) {
          fprintf(stderr, "unknown codec: %ls\n", args[3].c_str());
          return 2;
        }
      }
      const auto r = service.compressFile(
          std::filesystem::path(args[1]), std::filesystem::path(args[2]), options);
      printf("compressed %llu -> %llu bytes, crc 0x%08X\n",
             static_cast<unsigned long long>(r.inBytes),
             static_cast<unsigned long long>(r.outBytes), r.crc);
      fflush(stdout);
      return 0;
    }
    if (args[0] == L"decompress") {
      if (args.size() != 3) {
        fprintf(stderr, "usage: decompress <in> <out>\n");
        return 2;
      }
      const auto r = service.decompressFile(
          std::filesystem::path(args[1]), std::filesystem::path(args[2]));
      printf("decompressed %llu -> %llu bytes, verified %s\n",
             static_cast<unsigned long long>(r.inBytes),
             static_cast<unsigned long long>(r.outBytes),
             r.verified ? "yes" : "NO");
      fflush(stdout);
      return r.verified ? 0 : 3;
    }
  } catch (const std::exception &e) {
    fprintf(stderr, "engine error: %s\n", e.what());
    return 1;
  }
  if (args[0] == L"archive" && args.size() >= 3) {
    compression::ArchiveService archive;
    if (args[1] == L"create" && args.size() >= 4) {
      if (args.size() > 4096) {
        fprintf(stderr, "too many files\n");
        return 2;
      }
      archive::ArchiveBuildOptions options;
      std::vector<ArchiveEntrySource> entries;
      for (std::size_t i = 3; i < args.size(); ++i) {
        const std::filesystem::path p(args[i]);
        std::ifstream in(p, std::ios::binary);
        if (!in) {
          fprintf(stderr, "cannot open: %ls\n", args[i].c_str());
          return 1;
        }
        in.seekg(0, std::ios::end);
        const auto size = static_cast<std::size_t>(in.tellg());
        in.seekg(0, std::ios::beg);
        std::vector<uint8_t> data(size);
        in.read(reinterpret_cast<char *>(data.data()),
                static_cast<std::streamsize>(size));
        entries.push_back({p.filename().string(), std::move(data), 0});
      }
      archive.create(std::filesystem::path(args[2]), options, entries);
      printf("archive created: %zu entries\n", entries.size());
      fflush(stdout);
      return 0;
    }
    if (args[1] == L"verify") {
      const auto listing = archive.list(std::filesystem::path(args[2]));
      const auto results = archive.verify(std::filesystem::path(args[2]));
      bool allOk = true;
      for (const auto &v : results) {
        allOk = allOk && v.ok;
      }
      printf("archive verified: %zu entries, %zu blocks, %s\n",
             listing.entries.size(), results.size(), allOk ? "OK" : "FAILED");
      fflush(stdout);
      return allOk ? 0 : 3;
    }
  }
  fprintf(stderr, "unknown operation: %ls\n", args[0].c_str());
  return 2;
}

std::wstring ArgValue(const std::vector<std::wstring> &args, const wchar_t *key) {
  for (std::size_t i = 0; i + 1 < args.size(); ++i) {
    if (_wcsicmp(args[i].c_str(), key) == 0) {
      return args[i + 1];
    }
  }
  return L"";
}

// UI integration test: the REAL window is created and activated (XAML loads,
// controls instantiate, the strategy combo is populated from the registry),
// then the real controls and event handlers are driven exactly as a user
// would, and the XAML message loop pumps until the UI reports completion.
// Exit code signals success so CI can assert on it.
int RunUiTest(const std::vector<std::wstring> &args) {
  const std::wstring op = args.empty() ? L"" : args[0];
  const std::wstring inPath = ArgValue(args, L"--in");
  const std::wstring outPath = ArgValue(args, L"--out");
  const std::wstring codecName = ArgValue(args, L"--codec");
  const std::wstring resultPath = ArgValue(args, L"--result");
  const std::wstring entryArg = ArgValue(args, L"--entry");
  const bool isArchiveOp = op == L"archive-create" || op == L"archive-verify" ||
                           op == L"archive-extract";
  const bool isCompressOp = op == L"compress" || op == L"decompress";
  if ((!isCompressOp && !isArchiveOp) || inPath.empty() || resultPath.empty()) {
    fprintf(stderr,
            "usage: CompressorWindows --uitest <compress|decompress> "
            "--in <path> --out <path> --result <path> [--codec <name>]\n"
            "                 --uitest <archive-create|archive-verify|archive-extract> "
            "--in <path> [--out <path>] --result <path> [--entry <n>]\n");
    return 2;
  }

  // Spawn the real UI. Any failure must be reported via the result file and
  // shut down gracefully - an unhandled exception would pop a WER dialog and
  // hang CI.
  winrt::Microsoft::UI::Xaml::Window window{nullptr};
  winrt::CompressorWindows::implementation::MainWindow *ui = nullptr;
  try {
    window =
        winrt::make<CompressorWindows::implementation::MainWindow>();
    window.Activate();
    ui = window.as<winrt::CompressorWindows::implementation::MainWindow>().get();
  } catch (const std::exception &e) {
    fprintf(stderr, "uitest setup failed: %s\n", e.what());
    FILE *f = nullptr;
    if (_wfopen_s(&f, resultPath.c_str(), L"w") == 0 && f != nullptr) {
      fprintf(f, "FAIL setup: %s\n", e.what());
      fclose(f);
    }
    if (window) {
      window.Close();
    }
    winrt::Microsoft::UI::Xaml::Application::Current().Exit();
    return 1;
  }

  std::wstring status;
  bool ok = false;
  // Drive the real controls and trigger the real handlers.
  try {
  if (isCompressOp) {
    ui->InputPath().Text(winrt::hstring{inPath});
    ui->OutputPath().Text(winrt::hstring{outPath});
    if (op == L"compress" && !codecName.empty()) {
      const auto items = ui->StrategyCombo().Items();
      for (uint32_t i = 0; i < items.Size(); ++i) {
        if (winrt::unbox_value_or<winrt::hstring>(items.GetAt(i), L"") ==
            winrt::hstring{codecName}) {
          ui->StrategyCombo().SelectedIndex(i);
          break;
        }
      }
    }
    if (op == L"compress") {
      ui->OnCompressClick(winrt::box_value(L"uitest"),
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs{});
    } else {
      ui->OnDecompressClick(winrt::box_value(L"uitest"),
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs{});
    }
  } else {
    // Archive workflows run synchronously on the UI thread; only the result
    // file check below needs the pump.
    if (op == L"archive-create") {
      const std::wstring filesCsv = ArgValue(args, L"--files");
      std::vector<std::wstring> files;
      std::size_t start = 0;
      while (start <= filesCsv.size()) {
        const auto comma = filesCsv.find(L',', start);
        if (comma == std::wstring::npos) {
          files.push_back(filesCsv.substr(start));
          break;
        }
        files.push_back(filesCsv.substr(start, comma - start));
        start = comma + 1;
      }
      ui->DoArchiveCreate(inPath, files);
    } else if (op == L"archive-verify") {
      ui->DoArchiveOpen(inPath);
    } else if (op == L"archive-extract") {
      if (!ui->DoArchiveOpen(inPath)) {
        wprintf(L"ui status: archive open failed\n");
        fflush(stdout);
        FILE *f = nullptr;
        if (_wfopen_s(&f, resultPath.c_str(), L"w") == 0 && f != nullptr) {
          fprintf(f, "FAIL open\n");
          fclose(f);
        }
        window.Close();
        winrt::Microsoft::UI::Xaml::Application::Current().Exit();
        return 1;
      }
      const int32_t entry = _wtoi(entryArg.c_str());
      ui->DoArchiveExtract(inPath, entry, outPath);
    }
  }

  // Pump the XAML message loop until the UI reports completion.
  MSG msg{};
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  for (;;) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    status = std::wstring(ui->StatusText().Text());
    if (isArchiveOp) {
      status = std::wstring(ui->ArchiveStatusText().Text());
    }
    const bool finished =
        status.find(L"Compressed") != std::wstring::npos ||
        status.find(L"Decompressed") != std::wstring::npos ||
        status.find(L"Archive") != std::wstring::npos ||
        status.find(L"Extracted") != std::wstring::npos ||
        status.find(L"error") != std::wstring::npos ||
        status.find(L"Error") != std::wstring::npos;
    if (finished) {
      ok = status.find(L"error") == std::wstring::npos &&
           status.find(L"Error") == std::wstring::npos;
      break;
    }
    if (std::chrono::steady_clock::now() > deadline) {
      break;
    }
    Sleep(50);
  }
  } catch (const std::exception &e) {
    fprintf(stderr, "uitest workflow failed: %s\n", e.what());
    FILE *f = nullptr;
    if (_wfopen_s(&f, resultPath.c_str(), L"w") == 0 && f != nullptr) {
      fprintf(f, "FAIL workflow: %s\n", e.what());
      fclose(f);
    }
    window.Close();
    winrt::Microsoft::UI::Xaml::Application::Current().Exit();
    return 1;
  }
  const bool outputExists = std::filesystem::exists(std::filesystem::path(
      (op == L"archive-create" || op == L"archive-verify") ? inPath : outPath));
  const bool success = ok && outputExists;
  wprintf(L"ui status: %ls\n", status.c_str());
  fflush(stdout);
  // Report via a result file: XAML teardown on hard-exit paths is unreliable,
  // so the harness asserts on this file rather than the process exit code.
  FILE *resultFile = nullptr;
  if (_wfopen_s(&resultFile, resultPath.c_str(), L"w") == 0 && resultFile != nullptr) {
    fprintf(resultFile, success ? "OK %ls\n" : "FAIL %ls\n", status.c_str());
    fclose(resultFile);
  }
  window.Close();
  winrt::Microsoft::UI::Xaml::Application::Current().Exit();
  return success ? 0 : 1;
}

} // namespace

namespace winrt::CompressorWindows::implementation {
App::App() { InitializeComponent(); }

void App::OnLaunched(LaunchActivatedEventArgs const &) {
  int argc = 0;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv != nullptr && argc > 1 && wcscmp(argv[1], L"--uitest") == 0) {
    std::vector<std::wstring> args;
    for (int i = 2; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    ExitProcess(static_cast<UINT>(RunUiTest(args)));
  }
  if (argv != nullptr && argc > 1 &&
      (wcscmp(argv[1], L"compress") == 0 ||
       wcscmp(argv[1], L"decompress") == 0 ||
       wcscmp(argv[1], L"archive") == 0)) {
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    ExitProcess(static_cast<UINT>(RunHeadless(args)));
  }
  if (argv != nullptr) {
    LocalFree(argv);
  }
  window = make<MainWindow>();
  window.Activate();
}
} // namespace winrt::CompressorWindows::implementation