#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <compression/app/CompressionService.hpp>
#include <compression/codec/CodecRegistry.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <shellapi.h>
#include <string>
#include <thread>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {

// Headless automation mode for the real executable (used by CI end-to-end
// tests): CompressorWindows.exe compress <in> <out> [codec]
//                              decompress <in> <out>
// Runs the same facade the GUI uses; results go to stdout, exit code signals
// success/failure so tests can assert on it.
int RunHeadless(const std::vector<std::wstring> &args) {
  if (args.size() < 3 || args.size() > 4) {
    fprintf(stderr,
            "usage: CompressorWindows compress <in> <out> [codec]\n"
            "                 decompress <in> <out>\n");
    return 2;
  }
  try {
    compression::CompressionService service;
    if (args[0] == L"compress") {
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
  if ((op != L"compress" && op != L"decompress") || inPath.empty() ||
      outPath.empty()) {
    fprintf(stderr,
            "usage: CompressorWindows --uitest <compress|decompress> "
            "--in <path> --out <path> [--codec <name>]\n");
    return 2;
  }

  // Spawn the real UI.
  winrt::Microsoft::UI::Xaml::Window window =
      winrt::make<CompressorWindows::implementation::MainWindow>();
  window.Activate();
  auto ui = window.as<CompressorWindows::implementation::MainWindow>();

  // Drive the real controls.
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

  // Trigger the real click handler (background engine work + DispatcherQueue
  // marshaling back to the UI thread).
  if (op == L"compress") {
    ui->OnCompressClick(winrt::box_value(L"uitest"),
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs{});
  } else {
    ui->OnDecompressClick(winrt::box_value(L"uitest"),
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs{});
  }

  // Pump the XAML message loop until the UI reports completion.
  MSG msg{};
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
  std::wstring status;
  bool ok = false;
  for (;;) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    status = std::wstring(ui->StatusText().Text());
    const bool finished =
        status.find(L"Compressed") != std::wstring::npos ||
        status.find(L"Decompressed") != std::wstring::npos ||
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
  const bool outputExists = std::filesystem::exists(std::filesystem::path(outPath));
  wprintf(L"ui status: %ls\n", status.c_str());
  fflush(stdout);
  const int code = (!ok || !outputExists) ? 1 : 0;
  if (code != 0) {
    fprintf(stderr, "UI TEST FAILED (status='%ls', output=%s)\n",
            status.c_str(), outputExists ? "yes" : "no");
    fflush(stderr);
  }
  // Tear the XAML window down before exiting so the framework does not crash
  // during process teardown.
  window.Close();
  Sleep(500);
  return code;
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
       wcscmp(argv[1], L"decompress") == 0)) {
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