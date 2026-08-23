#pragma once

#include "MainWindow.xaml.g.h"

#include <compression/FileFormat.hpp>

#include <string>
#include <vector>

namespace winrt::CompressorWindows::implementation {
struct MainWindow : MainWindowT<MainWindow> {
  MainWindow();

  void OnCompressClick(winrt::Windows::Foundation::IInspectable const &,
                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);
  void OnDecompressClick(winrt::Windows::Foundation::IInspectable const &,
                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const &);

private:
  void Run(bool compress);
  void SetStatus(std::wstring text);

  winrt::Microsoft::UI::Dispatching::DispatcherQueue queue_{nullptr};
  std::vector<compression::format::AlgorithmID> strategyIds_;
};
} // namespace winrt::CompressorWindows::implementation