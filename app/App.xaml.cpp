#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <compression/app/CompressionService.hpp>
#include <compression/codec/CodecRegistry.hpp>

#include <filesystem>
#include <shellapi.h>
#include <string>
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

} // namespace

namespace winrt::CompressorWindows::implementation {
App::App() { InitializeComponent(); }

void App::OnLaunched(LaunchActivatedEventArgs const &) {
  int argc = 0;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
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