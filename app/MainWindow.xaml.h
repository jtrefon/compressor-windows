#pragma once

#include "MainWindow.g.h"

#include <compression/FileFormat.hpp>
#include <compression/app/ArchiveService.hpp>
#include <compression/events/EventBus.hpp>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

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

  // Picker-free entry points (used by the --uitest harness).
  bool DoArchiveCreate(const std::wstring &outPath,
                       const std::vector<std::wstring> &files);
  bool DoArchiveOpen(const std::wstring &archivePath);
  bool DoArchiveExtract(const std::wstring &archivePath, int32_t entryIndex,
                        const std::wstring &outDir);

private:
  void Run(bool compress);
  void SetStatus(std::wstring text);
  void SetArchiveStatus(std::wstring text);

  winrt::Microsoft::UI::Dispatching::DispatcherQueue queue_{nullptr};
  std::vector<compression::format::AlgorithmID> strategyIds_;
  std::shared_ptr<compression::events::EventBus> bus_;
  std::shared_ptr<compression::events::IEventListener> listener_;
  std::vector<compression::archive::ArchiveEntry> entries_;
};
} // namespace winrt::CompressorWindows::implementation