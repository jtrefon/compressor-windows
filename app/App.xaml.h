#pragma once

#include "App.xaml.g.h"

namespace winrt::CompressorWindows::implementation {
struct App : AppT<App> {
  App();

  void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const &);

private:
  winrt::Microsoft::UI::Xaml::Window window{nullptr};
};
} // namespace winrt::CompressorWindows::implementation