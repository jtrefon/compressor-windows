#include "pch.h"
#include "MainWindow.xaml.h"

#include <compression/app/CompressionService.hpp>
#include <compression/codec/CodecRegistry.hpp>

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.Windows.Storage.Pickers.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace {

// Marshals engine progress events (worker threads) to the UI thread.
class UiEventListener final : public compression::events::IEventListener {
public:
  UiEventListener(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
                 std::function<void(uint8_t)> onProgress)
      : queue_(queue), onProgress_(std::move(onProgress)) {}

  void onEvent(const compression::events::CompressionEvent &event) override {
    if (event.type == compression::events::EventType::ChunkProgress) {
      queue_.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler{
          [this, pct = event.progressPct]() { onProgress_(pct); }});
    }
  }

private:
  winrt::Microsoft::UI::Dispatching::DispatcherQueue queue_{nullptr};
  std::function<void(uint8_t)> onProgress_;
};

HWND WindowHandle(winrt::Microsoft::UI::Xaml::Window const &window) {
  auto windowNative = window.as<::IWindowNative>();
  HWND hwnd{0};
  winrt::check_hresult(windowNative->get_WindowHandle(&hwnd));
  return hwnd;
}

std::wstring Widen(const std::string &s) {
  return std::wstring(s.begin(), s.end());
}

} // namespace

namespace winrt::CompressorWindows::implementation {

MainWindow::MainWindow() {
  InitializeComponent();

  queue_ =
      winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();

  // Strategy list is generated from the engine's CodecRegistry — new codecs
  // appear here automatically (Open/Closed Principle).
  for (const auto &[id, name] : compression::codec::CodecRegistry::instance().all()) {
    strategyIds_.push_back(id);
    StrategyCombo().Items().Append(box_value(to_hstring(name)));
  }
  if (StrategyCombo().Items().Size() > 0) {
    StrategyCombo().SelectedIndex(0);
  }
  for (uint32_t i = 0; i < StrategyCombo().Items().Size(); ++i) {
    if (winrt::unbox_value_or<winrt::hstring>(StrategyCombo().Items().GetAt(i), L"") ==
        L"optimized") {
      StrategyCombo().SelectedIndex(i);
      break;
    }
  }

  // Progress events -> progress bar.
  bus_ = std::make_shared<compression::events::EventBus>();
  listener_ = std::make_shared<UiEventListener>(queue_, [this](uint8_t pct) {
    Progress().Value(pct);
  });
  bus_->subscribe(listener_);

  // App icon (from the embedded resource).
  const HWND hwnd = WindowHandle(*this);
  const HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
  if (icon != nullptr) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
  }
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
        [this, text = std::move(text)]() { StatusText().Text(to_hstring(text)); }});
  }
}

void MainWindow::SetArchiveStatus(std::wstring text) {
  if (queue_) {
    queue_.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler{
        [this, text = std::move(text)]() { ArchiveStatusText().Text(to_hstring(text)); }});
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
      compression::CompressionService service(bus_);
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

// --- File pickers (unpackaged WinUI 3: InitializeWithWindow is required) ---

winrt::fire_and_forget MainWindow::OnBrowseInClick(IInspectable const &,
                                                   RoutedEventArgs const &) {
  auto picker = winrt::Microsoft::Windows::Storage::Pickers::FileOpenPicker{};
  picker.FileTypeFilter().Append(L"*");
  picker.InitializeWithWindow(WindowHandle(*this));
  auto file = co_await picker.PickSingleFileAsync();
  if (file) {
    InputPath().Text(file.Path());
  }
}

winrt::fire_and_forget MainWindow::OnBrowseOutClick(IInspectable const &,
                                                    RoutedEventArgs const &) {
  auto picker = winrt::Microsoft::Windows::Storage::Pickers::FileSavePicker{};
  picker.FileTypeChoices().Insert(L"Compressed files",
                                  winrt::single_threaded_vector<hstring>({L"*"}));
  picker.SuggestedFileName(L"output");
  picker.InitializeWithWindow(WindowHandle(*this));
  auto file = co_await picker.PickSaveFileAsync();
  if (file) {
    OutputPath().Text(file.Path());
  }
}

winrt::fire_and_forget MainWindow::OnBrowseArchiveClick(IInspectable const &,
                                                        RoutedEventArgs const &) {
  auto picker = winrt::Microsoft::Windows::Storage::Pickers::FileOpenPicker{};
  picker.FileTypeFilter().Append(L".cza");
  picker.FileTypeFilter().Append(L"*");
  picker.InitializeWithWindow(WindowHandle(*this));
  auto file = co_await picker.PickSingleFileAsync();
  if (file) {
    ArchivePath().Text(file.Path());
  }
}

winrt::fire_and_forget MainWindow::OnCreateArchiveClick(IInspectable const &,
                                                        RoutedEventArgs const &) {
  auto picker = winrt::Microsoft::Windows::Storage::Pickers::FileOpenPicker{};
  picker.FileTypeFilter().Append(L"*");
  picker.InitializeWithWindow(WindowHandle(*this));
  auto files = co_await picker.PickMultipleFilesAsync();
  if (files && files.Size() > 0) {
    std::vector<std::wstring> paths;
    for (const auto &f : files) {
      paths.push_back(std::wstring(f.Path()));
    }
    const std::wstring outPath(ArchivePath().Text());
    if (outPath.empty()) {
      SetArchiveStatus(L"Choose an archive file first.");
      co_return;
    }
    DoArchiveCreate(outPath, paths);
  }
}

winrt::fire_and_forget MainWindow::OnOpenArchiveClick(IInspectable const &,
                                                      RoutedEventArgs const &) {
  const std::wstring archivePath(ArchivePath().Text());
  if (archivePath.empty()) {
    SetArchiveStatus(L"Choose an archive file first.");
    co_return;
  }
  DoArchiveOpen(archivePath);
}

winrt::fire_and_forget MainWindow::OnExtractArchiveClick(IInspectable const &,
                                                         RoutedEventArgs const &) {
  const std::wstring archivePath(ArchivePath().Text());
  if (archivePath.empty() || entries_.empty()) {
    SetArchiveStatus(L"Open an archive with entries first.");
    co_return;
  }
  auto picker = winrt::Microsoft::Windows::Storage::Pickers::FolderPicker{};
  picker.FileTypeFilter().Append(L"*");
  picker.InitializeWithWindow(WindowHandle(*this));
  auto folder = co_await picker.PickSingleFolderAsync();
  if (folder) {
    const int32_t index = static_cast<int32_t>(ArchiveEntries().SelectedIndex());
    DoArchiveExtract(archivePath, index, std::wstring(folder.Path()));
  }
}

// --- Archive operations (also used by the --uitest harness) ---

bool MainWindow::DoArchiveCreate(const std::wstring &outPath,
                                 const std::vector<std::wstring> &files) {
  try {
    compression::ArchiveService archive(bus_);
    archive::ArchiveBuildOptions options;
    std::vector<ArchiveEntrySource> entries;
    for (const auto &path : files) {
      const std::filesystem::path p(path);
      std::ifstream in(p, std::ios::binary);
      if (!in) {
        SetArchiveStatus(L"Cannot open: " + path);
        return false;
      }
      in.seekg(0, std::ios::end);
      const auto size = static_cast<std::size_t>(in.tellg());
      in.seekg(0, std::ios::beg);
      std::vector<uint8_t> data(size);
      in.read(reinterpret_cast<char *>(data.data()),
              static_cast<std::streamsize>(size));
      entries.push_back(
          {p.filename().string(), std::move(data), 0});
    }
    archive.create(std::filesystem::path(outPath), options, entries);
    wchar_t buf[128];
    swprintf_s(buf, L"Archive created: %zu entries -> %ls", entries.size(),
               outPath.c_str());
    SetArchiveStatus(buf);
    return true;
  } catch (const std::exception &e) {
    std::wstring msg(e.what(), e.what() + strlen(e.what()));
    SetArchiveStatus(L"Archive error: " + msg);
    return false;
  }
}

bool MainWindow::DoArchiveOpen(const std::wstring &archivePath) {
  try {
    compression::ArchiveService archive(bus_);
    const auto listing = archive.list(std::filesystem::path(archivePath));
    entries_ = listing.entries;
    ArchiveEntries().Items().Clear();
    for (const auto &e : entries_) {
      wchar_t buf[256];
      swprintf_s(buf, L"%ls  (%llu bytes)",
                 Widen(e.name).c_str(),
                 static_cast<unsigned long long>(e.rawSize));
      ArchiveEntries().Items().Append(box_value(to_hstring(buf)));
    }
    const auto verifyResults = archive.verify(std::filesystem::path(archivePath));
    bool allOk = true;
    for (const auto &v : verifyResults) {
      allOk = allOk && v.ok;
    }
    wchar_t buf[256];
    swprintf_s(buf, L"Archive opened: %zu entries, %zu blocks, verify: %ls",
               entries_.size(), verifyResults.size(), allOk ? L"OK" : L"FAILED");
    SetArchiveStatus(buf);
    return allOk;
  } catch (const std::exception &e) {
    std::wstring msg(e.what(), e.what() + strlen(e.what()));
    SetArchiveStatus(L"Archive error: " + msg);
    return false;
  }
}

bool MainWindow::DoArchiveExtract(const std::wstring &archivePath,
                                  int32_t entryIndex,
                                  const std::wstring &outDir) {
  try {
    if (entryIndex < 0 ||
        entryIndex >= static_cast<int32_t>(entries_.size())) {
      SetArchiveStatus(L"Select an entry to extract.");
      return false;
    }
    compression::ArchiveService archive(bus_);
    const auto result = archive.extract(
        std::filesystem::path(archivePath), entries_[entryIndex].id,
        std::filesystem::path(outDir));
    wchar_t buf[256];
    swprintf_s(buf, L"Extracted %llu -> %llu bytes, CRC verified: %ls",
               static_cast<unsigned long long>(result.inBytes),
               static_cast<unsigned long long>(result.outBytes),
               result.verified ? L"yes" : L"NO");
    SetArchiveStatus(buf);
    return result.verified;
  } catch (const std::exception &e) {
    std::wstring msg(e.what(), e.what() + strlen(e.what()));
    SetArchiveStatus(L"Archive error: " + msg);
    return false;
  }
}

} // namespace winrt::CompressorWindows::implementation