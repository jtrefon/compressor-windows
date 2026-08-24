#pragma once

#include "MainWindow.g.h"
#include "UpdateService.h"

#include <compression/FileFormat.hpp>
#include <compression/app/ArchiveService.hpp>
#include <compression/events/EventBus.hpp>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace winrt::CompressorWindows::implementation {
struct MainWindow : MainWindowT<MainWindow> {
  MainWindow();

  // Compress / decompress handlers (driven by the --uitest harness too).
  void OnCompressClick(winrt::Windows::Foundation::IInspectable const &,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnDecompressClick(winrt::Windows::Foundation::IInspectable const &,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnBrowseInClick(winrt::Windows::Foundation::IInspectable const &,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnBrowseOutClick(winrt::Windows::Foundation::IInspectable const &,
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);

  // Archive handlers.
  void OnBrowseArchiveClick(winrt::Windows::Foundation::IInspectable const &,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnCreateArchiveClick(winrt::Windows::Foundation::IInspectable const &,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnOpenArchiveClick(winrt::Windows::Foundation::IInspectable const &,
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnExtractArchiveClick(winrt::Windows::Foundation::IInspectable const &,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);

  void OnCheckUpdatesClick(winrt::Windows::Foundation::IInspectable const &,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  winrt::Windows::Foundation::IAsyncAction CheckForUpdatesAsync(bool showWhenCurrent);
  winrt::Windows::Foundation::IAsyncAction DownloadAndInstallUpdateAsync(
      const updates::UpdateInfo &info);
  void OnCancelClick(winrt::Windows::Foundation::IInspectable const &,
                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnDragOver(winrt::Windows::Foundation::IInspectable const &,
                  winrt::Microsoft::UI::Xaml::DragEventArgs const &);
  void OnDrop(winrt::Windows::Foundation::IInspectable const &,
              winrt::Microsoft::UI::Xaml::DragEventArgs const &);
  void PrefillInput(const std::wstring &path);

  // Picker-free entry points (used by the --uitest harness).
  bool DoArchiveCreate(const std::wstring &outPath,
                       const std::vector<std::wstring> &files);
  bool DoArchiveOpen(const std::wstring &archivePath);
  bool DoArchiveExtract(const std::wstring &archivePath, int32_t entryIndex,
                        const std::wstring &outDir);

private:
  void Run(bool compress);
  std::wstring FormatLiveStatus(uint8_t pct, uint64_t in, uint64_t out);
  std::wstring DefaultOutputPath(const std::wstring &inPath, bool compress);
  std::wstring PickArchiveFile(HWND hwnd);
  void PrefillOutputFromInput(const std::wstring &inPath, bool compress);
  void ApplyTitleBarTheme();
  void SetStatus(std::wstring text);
  void SetArchiveStatus(std::wstring text);

  winrt::Microsoft::UI::Dispatching::DispatcherQueue queue_{nullptr};
  std::vector<compression::format::AlgorithmID> strategyIds_;
  std::atomic<bool> busy_{false};
  std::atomic<bool> cancel_{false};
  std::atomic<bool> compressMode_{true};
  std::shared_ptr<compression::events::EventBus> bus_;
  std::shared_ptr<compression::events::IEventListener> listener_;
  std::vector<compression::archive::ArchiveEntry> entries_;
  bool streamMode_ = false;  // archive section is holding a single-file .cpz
  std::wstring streamName_;  // original file name of that stream
};
} // namespace winrt::CompressorWindows::implementation