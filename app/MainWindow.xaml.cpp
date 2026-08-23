#include "pch.h"
#include "MainWindow.xaml.h"
#include "UpdateService.h"

#include <compression/app/CompressionService.hpp>
#include <compression/codec/CodecRegistry.hpp>

#include <winrt/Windows.System.h>

#include <microsoft.ui.xaml.window.h>
#include <shobjidl.h>

using namespace compression;

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

// Marshals engine progress events (worker threads) to the UI thread,
// accumulating per-chunk bytes so the UI can show a live ratio.
class UiEventListener final : public compression::events::IEventListener {
public:
  UiEventListener(winrt::Microsoft::UI::Dispatching::DispatcherQueue queue,
                 std::function<void(uint8_t, uint64_t, uint64_t)> onProgress)
      : queue_(queue), onProgress_(std::move(onProgress)) {}

  void onEvent(const compression::events::CompressionEvent &event) override {
    if (event.type == compression::events::EventType::ChunkProgress) {
      bytesIn_ += event.bytesIn;
      bytesOut_ += event.bytesOut;
      queue_.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler{
          [this, pct = event.progressPct]() {
            onProgress_(pct, bytesIn_, bytesOut_);
          }});
    }
  }

private:
  winrt::Microsoft::UI::Dispatching::DispatcherQueue queue_{nullptr};
  std::function<void(uint8_t, uint64_t, uint64_t)> onProgress_;
  uint64_t bytesIn_ = 0;
  uint64_t bytesOut_ = 0;
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

std::wstring ShellItemPath(IShellItem *item) {
  PWSTR raw = nullptr;
  winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &raw));
  std::wstring result(raw ? raw : L"");
  CoTaskMemFree(raw);
  return result;
}

// Native file/folder dialogs (stable Win32 API; identical to the WinRT
// pickers under the hood).
std::vector<std::wstring> PickFiles(HWND hwnd, bool folders, bool multi) {
  winrt::com_ptr<IFileOpenDialog> dlg;
  winrt::check_hresult(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                        CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(dlg.put())));
  DWORD opts = FOS_FORCEFILESYSTEM;
  if (folders) {
    opts |= FOS_PICKFOLDERS;
  }
  if (multi) {
    opts |= FOS_ALLOWMULTISELECT;
  }
  winrt::check_hresult(dlg->SetOptions(opts));
  std::vector<std::wstring> result;
  if (FAILED(dlg->Show(hwnd))) {
    return result;
  }
  winrt::com_ptr<IShellItemArray> items;
  winrt::check_hresult(dlg->GetResults(items.put()));
  DWORD count = 0;
  winrt::check_hresult(items->GetCount(&count));
  for (DWORD i = 0; i < count; ++i) {
    winrt::com_ptr<IShellItem> item;
    winrt::check_hresult(items->GetItemAt(i, item.put()));
    result.push_back(ShellItemPath(item.get()));
  }
  return result;
}

std::wstring PickSaveFile(HWND hwnd) {
  winrt::com_ptr<IFileSaveDialog> dlg;
  winrt::check_hresult(CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                        CLSCTX_INPROC_SERVER,
                                        IID_PPV_ARGS(dlg.put())));
  winrt::check_hresult(dlg->SetOptions(FOS_FORCEFILESYSTEM));
  const COMDLG_FILTERSPEC types[] = {
      {L"Compressor files", L"*.cpz"}, {L"All files", L"*.*"}};
  winrt::check_hresult(dlg->SetFileTypes(2, types));
  winrt::check_hresult(dlg->SetDefaultExtension(L"cpz"));
  if (FAILED(dlg->Show(hwnd))) {
    return L"";
  }
  winrt::com_ptr<IShellItem> item;
  winrt::check_hresult(dlg->GetResult(item.put()));
  return ShellItemPath(item.get());
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

  // Threads selector: Auto(0), 1, 2, 4, 8.
  for (const wchar_t *t : {L"Auto", L"1", L"2", L"4", L"8"}) {
    ThreadsCombo().Items().Append(box_value(winrt::hstring{t}));
  }
  ThreadsCombo().SelectedIndex(0);

  // Progress events -> progress bar + live ratio line.
  bus_ = std::make_shared<compression::events::EventBus>();
  listener_ = std::make_shared<UiEventListener>(
      queue_, [this](uint8_t pct, uint64_t in, uint64_t out) {
        Progress().Value(pct);
        LiveStatusText().Text(winrt::hstring{FormatLiveStatus(pct, in, out)});
      });
  bus_->subscribe(listener_);

  // App icon (from the embedded resource).
  const HWND hwnd = WindowHandle(*this);
  const HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
  if (icon != nullptr) {
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
  }

  VersionText().Text(winrt::hstring{L"version "} +
                     winrt::hstring{updates::kAppVersion});

  // Quiet update check shortly after launch (skipped in automation modes).
  int argc = 0;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool automated = false;
  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--uitest") == 0 || wcscmp(argv[i], L"compress") == 0 ||
        wcscmp(argv[i], L"decompress") == 0 || wcscmp(argv[i], L"archive") == 0 ||
        wcscmp(argv[i], L"--version") == 0) {
      automated = true;
      break;
    }
  }
  if (argv != nullptr) {
    LocalFree(argv);
  }
  if (!automated && queue_) {
    queue_.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler{
        [this]() { CheckForUpdatesAsync(false); }});
  }
}

void MainWindow::OnCheckUpdatesClick(IInspectable const &,
                                     RoutedEventArgs const &) {
  CheckForUpdatesAsync(true);
}

winrt::Windows::Foundation::IAsyncAction MainWindow::CheckForUpdatesAsync(bool showWhenCurrent) {
  updates::UpdateInfo info;
  const bool available = co_await updates::CheckForUpdate(info);
  winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
  dialog.XamlRoot(Content().XamlRoot());
  if (info.available) {
    dialog.Title(winrt::box_value(winrt::hstring{L"Update available: "} +
                                  winrt::hstring{info.version}));
    dialog.Content(winrt::box_value(winrt::hstring{info.notes.empty() ? L"A new version is available." : info.notes}));
    dialog.PrimaryButtonText(L"Download");
    dialog.CloseButtonText(L"Later");
    dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
    const auto result = co_await dialog.ShowAsync();
    if (result == winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary &&
        !info.downloadUrl.empty()) {
      co_await winrt::Windows::System::Launcher::LaunchUriAsync(
          winrt::Windows::Foundation::Uri{info.downloadUrl});
    }
  } else if (showWhenCurrent) {
    dialog.Title(winrt::box_value(winrt::hstring{L"You are up to date"}));
    dialog.Content(winrt::box_value(winrt::hstring{L"Version "} +
                                    winrt::hstring{updates::kAppVersion} +
                                    L" is the latest release."));
    dialog.CloseButtonText(L"OK");
    co_await dialog.ShowAsync();
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
        [this, text = std::move(text)]() { StatusText().Text(winrt::hstring{text}); }});
  }
}

void MainWindow::SetArchiveStatus(std::wstring text) {
  if (queue_) {
    queue_.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler{
        [this, text = std::move(text)]() { ArchiveStatusText().Text(winrt::hstring{text}); }});
  }
}

void MainWindow::Run(bool compress) {
  if (busy_) {
    SetStatus(L"An operation is already running.");
    return;
  }
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
  static const int kThreads[] = {0, 1, 2, 4, 8};
  const int threadIndex = static_cast<int>(ThreadsCombo().SelectedIndex());
  const int threads =
      (threadIndex >= 0 && threadIndex < 5) ? kThreads[threadIndex] : 0;

  busy_ = true;
  cancel_ = false;
  compressMode_ = compress;
  CompressBtn().IsEnabled(false);
  DecompressBtn().IsEnabled(false);
  CancelBtn().IsEnabled(true);
  Progress().Value(0);
  LiveStatusText().Text(L"");

  // Engine work runs off the UI thread; results marshal back via the
  // DispatcherQueue.
  std::thread([this, compress, codec, threads, inPath, outPath]() {
    try {
      compression::CompressionService service(bus_);
      if (compress) {
        compression::CompressionOptions options;
        options.codec = codec;
        options.threads = static_cast<std::size_t>(threads);
        const auto r = service.compressFile(
            std::filesystem::path(inPath), std::filesystem::path(outPath), options);
        if (cancel_) {
          std::error_code ec;
          std::filesystem::remove(std::filesystem::path(outPath), ec);
          SetStatus(L"Cancelled - partial output removed.");
        } else {
          wchar_t buf[256];
          swprintf_s(buf, L"Compressed %llu -> %llu bytes (ratio %.2f%%, CRC 0x%08X)",
                     static_cast<unsigned long long>(r.inBytes),
                     static_cast<unsigned long long>(r.outBytes),
                     r.ratio * 100.0, r.crc);
          SetStatus(buf);
        }
      } else {
        const auto r = service.decompressFile(
            std::filesystem::path(inPath), std::filesystem::path(outPath));
        if (cancel_) {
          std::error_code ec;
          std::filesystem::remove(std::filesystem::path(outPath), ec);
          SetStatus(L"Cancelled - partial output removed.");
        } else {
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
      }
    } catch (const std::exception &e) {
      std::wstring msg(e.what(), e.what() + strlen(e.what()));
      SetStatus(L"Engine error: " + msg);
    }
    queue_.TryEnqueue(winrt::Microsoft::UI::Dispatching::DispatcherQueueHandler{
        [this]() {
          busy_ = false;
          CompressBtn().IsEnabled(true);
          DecompressBtn().IsEnabled(true);
          CancelBtn().IsEnabled(false);
        }});
  }).detach();
}

void MainWindow::OnCancelClick(IInspectable const &, RoutedEventArgs const &) {
  if (busy_) {
    cancel_ = true;
    CancelBtn().IsEnabled(false);
    LiveStatusText().Text(L"Cancelling... (finishes the current block)");
  }
}

std::wstring MainWindow::FormatLiveStatus(uint8_t pct, uint64_t in, uint64_t out) {
  auto fmt = [](uint64_t v) {
    wchar_t buf[32];
    if (v >= 1024ull * 1024 * 1024) {
      swprintf_s(buf, L"%.1f GB", static_cast<double>(v) / (1024 * 1024 * 1024));
    } else if (v >= 1024ull * 1024) {
      swprintf_s(buf, L"%.1f MB", static_cast<double>(v) / (1024 * 1024));
    } else if (v >= 1024) {
      swprintf_s(buf, L"%.1f KB", static_cast<double>(v) / 1024);
    } else {
      swprintf_s(buf, L"%llu B", static_cast<unsigned long long>(v));
    }
    return std::wstring(buf);
  };
  wchar_t buf[160];
  if (compressMode_) {
    const double ratio = (in > 0) ? (static_cast<double>(out) * 100.0 / in) : 0.0;
    swprintf_s(buf, L"Compressing %u%%  |  %ls -> %ls  |  ratio %.1f%%",
               static_cast<unsigned>(pct), fmt(in).c_str(), fmt(out).c_str(), ratio);
  } else {
    swprintf_s(buf, L"Decompressing %u%%  |  %ls -> %ls",
               static_cast<unsigned>(pct), fmt(in).c_str(), fmt(out).c_str());
  }
  return std::wstring(buf);
}

// --- File pickers (native IFileDialog; no WinRT picker projection needed) ---

void MainWindow::OnBrowseInClick(IInspectable const &, RoutedEventArgs const &) {
  const auto files = PickFiles(WindowHandle(*this), false, false);
  if (!files.empty()) {
    InputPath().Text(winrt::hstring{files[0]});
  }
}

void MainWindow::OnBrowseOutClick(IInspectable const &, RoutedEventArgs const &) {
  const std::wstring path = PickSaveFile(WindowHandle(*this));
  if (!path.empty()) {
    OutputPath().Text(winrt::hstring{path});
  }
}

void MainWindow::OnBrowseArchiveClick(IInspectable const &,
                                      RoutedEventArgs const &) {
  const auto files = PickFiles(WindowHandle(*this), false, false);
  if (!files.empty()) {
    ArchivePath().Text(winrt::hstring{files[0]});
  }
}

void MainWindow::OnCreateArchiveClick(IInspectable const &,
                                      RoutedEventArgs const &) {
  const auto paths = PickFiles(WindowHandle(*this), false, true);
  if (paths.empty()) {
    return;
  }
  const std::wstring outPath(ArchivePath().Text());
  if (outPath.empty()) {
    SetArchiveStatus(L"Choose an archive file first.");
    return;
  }
  DoArchiveCreate(outPath, paths);
}

void MainWindow::OnOpenArchiveClick(IInspectable const &,
                                    RoutedEventArgs const &) {
  const std::wstring archivePath(ArchivePath().Text());
  if (archivePath.empty()) {
    SetArchiveStatus(L"Choose an archive file first.");
    return;
  }
  DoArchiveOpen(archivePath);
}

void MainWindow::OnExtractArchiveClick(IInspectable const &,
                                       RoutedEventArgs const &) {
  const std::wstring archivePath(ArchivePath().Text());
  if (archivePath.empty() || entries_.empty()) {
    SetArchiveStatus(L"Open an archive with entries first.");
    return;
  }
  const auto folders = PickFiles(WindowHandle(*this), true, false);
  if (folders.empty()) {
    return;
  }
  const int32_t index = static_cast<int32_t>(ArchiveEntries().SelectedIndex());
  DoArchiveExtract(archivePath, index, folders[0]);
}

// --- Drag & drop + launch-with-file ---

void MainWindow::OnDragOver(IInspectable const &,
                            winrt::Microsoft::UI::Xaml::DragEventArgs const &e) {
  e.AcceptedOperation(winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation::Copy);
}

void MainWindow::OnDrop(IInspectable const &,
                        winrt::Microsoft::UI::Xaml::DragEventArgs const &e) {
  try {
    const auto items = e.DataView().GetStorageItemsAsync().get();
    if (items.Size() == 0) {
      return;
    }
    std::wstring first;
    for (const auto &item : items) {
      if (!first.empty()) {
        break;
      }
      if (const auto file = item.try_as<winrt::Windows::Storage::StorageFile>()) {
        first = std::wstring(file.Path());
      }
    }
    if (!first.empty()) {
      InputPath().Text(winrt::hstring{first});
      if (items.Size() > 1) {
        SetStatus(L"Dropped " + std::to_wstring(items.Size()) +
                  L" files - use the Archive section to bundle them.");
      } else {
        SetStatus(L"File loaded from drop.");
      }
    }
  } catch (...) {
    SetStatus(L"Could not read the dropped files.");
  }
}

void MainWindow::PrefillInput(const std::wstring &path) {
  InputPath().Text(winrt::hstring{path});
  SetStatus(L"File opened: " + path);
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
      ArchiveEntries().Items().Append(box_value(winrt::hstring{buf}));
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
// Factory hook expected by the C++/WinRT generated module.g.cpp (normally
// emitted by the component projection, which does not run for this project).
void *__cdecl winrt_make_CompressorWindows_MainWindow(void) {
  return winrt::detach_abi(
      winrt::make<winrt::CompressorWindows::implementation::MainWindow>());
}
