#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <compression/app/CompressionService.hpp>
#include <compression/codec/CodecRegistry.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::CompressorWindows::implementation {

MainWindow::MainWindow() {
  InitializeComponent();

  // Strategy list is generated from the engine's CodecRegistry — new codecs
  // appear here automatically (Open/Closed Principle).
  for (const auto &[id, name] : compression::codec::CodecRegistry::instance().all()) {
    strategyIds_.push_back(id);
    StrategyCombo().Items().Append(box_value(to_hstring(name)));
  }
  if (StrategyCombo().Items().Size() > 0) {
    StrategyCombo().SelectedIndex(0);
  }

  queue_ = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
}

void MainWindow::OnCompressClick(IInspectable const &, RoutedEventArgs const &) {
  Run(true);
}

void MainWindow::OnDecompressClick(IInspectable const &, RoutedEventArgs const &) {
  Run(false);
}

void MainWindow::SetStatus(std::wstring text) {
  if (queue_) {
    queue_.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler{
        [this, text = std::move(text)]() {
          StatusText().Text(to_hstring(text));
        }});
  }
}

void MainWindow::Run(bool compress) {
  const std::wstring inPath(InputPath().Text());
  const std::wstring outPath(OutputPath().Text());
  if (inPath.empty() || outPath.empty()) {
    SetStatus(L"Choose an input and an output file first.");
    return;
  }

  const int index = static_cast<int>(StrategyCombo().SelectedIndex());
  const compression::format::AlgorithmID codec =
      (index >= 0 && index < static_cast<int>(strategyIds_.size()))
          ? strategyIds_[static_cast<std::size_t>(index)]
          : compression::format::AlgorithmID::OPTIMIZED_COMPRESSOR;

  // Engine work runs off the UI thread; results marshal back via the
  // DispatcherQueue.
  std::thread([this, compress, codec, inPath, outPath]() {
    try {
      compression::CompressionService service;
      if (compress) {
        compression::CompressionOptions options;
        options.codec = codec;
        const auto r = service.compressFile(
            std::filesystem::path(inPath), std::filesystem::path(outPath), options);
        wchar_t buf[256];
        swprintf_s(buf, L"Compressed %llu -> %llu bytes (ratio %.2f%%, CRC 0x%08X)",
                   static_cast<unsigned long long>(r.inBytes),
                   static_cast<unsigned long long>(r.outBytes),
                   r.ratio * 100.0, r.crc);
        SetStatus(buf);
      } else {
        const auto r = service.decompressFile(
            std::filesystem::path(inPath), std::filesystem::path(outPath));
        wchar_t buf[256];
        swprintf_s(buf, L"Decompressed %llu -> %llu bytes, CRC verified: %ls",
                   static_cast<unsigned long long>(r.inBytes),
                   static_cast<unsigned long long>(r.outBytes),
                   r.verified ? L"yes" : L"NO");
        SetStatus(buf);
        if (!r.verified) {
          SetStatus(L"Decompressed data FAILED CRC verification!");
        }
      }
    } catch (const std::exception &e) {
      std::wstring msg(e.what(), e.what() + strlen(e.what()));
      SetStatus(L"Engine error: " + msg);
    }
  }).detach();
}

} // namespace winrt::CompressorWindows::implementation